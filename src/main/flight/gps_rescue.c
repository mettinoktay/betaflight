/*
 * This file is part of Betaflight.
 *
 * Betaflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Betaflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Betaflight. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "platform.h"

#ifdef USE_GPS_RESCUE

#include "build/debug.h"

#include "common/axis.h"
#include "common/filter.h"
#include "common/maths.h"
#include "common/utils.h"

#include "drivers/time.h"

#include "io/gps.h"

#include "config/config.h"
#include "fc/core.h"
#include "fc/rc_controls.h"
#include "fc/rc_modes.h"
#include "fc/runtime_config.h"

#include "flight/failsafe.h"
#include "flight/imu.h"
#include "flight/pid.h"
#include "flight/position.h"

#include "rx/rx.h"

#include "sensors/acceleration.h"

#include "gps_rescue.h"

typedef enum {
    RESCUE_IDLE,
    RESCUE_INITIALIZE,
    RESCUE_ATTAIN_ALT,
    RESCUE_ROTATE,
    RESCUE_FLY_HOME,
    RESCUE_DESCENT,
    RESCUE_LANDING,
    RESCUE_ABORT,
    RESCUE_COMPLETE,
    RESCUE_DO_NOTHING
} rescuePhase_e;

typedef enum {
    RESCUE_HEALTHY,
    RESCUE_FLYAWAY,
    RESCUE_GPSLOST,
    RESCUE_LOWSATS,
    RESCUE_CRASH_FLIP_DETECTED,
    RESCUE_STALLED,
    RESCUE_TOO_CLOSE,
    RESCUE_NO_HOME_POINT
} rescueFailureState_e;

typedef struct {
    float maxAltitudeCm;
    float returnAltitudeCm;
    float targetAltitudeCm;
    float targetLandingAltitudeCm;
    float targetVelocityCmS;
    float pitchAngleLimitDeg;
    float rollAngleLimitDeg;
    float descentDistanceM;
    int8_t secondsFailing;
    float altitudeStep;
    float descentRateModifier;
    float yawAttenuator;
    float disarmThreshold;
    float velocityITermAccumulator;
    float velocityPidCutoff;
    float velocityPidCutoffModifier;
    float proximityToLandingArea;
    float velocityItermRelax;
} rescueIntent_s;

typedef struct {
    float currentAltitudeCm;
    float distanceToHomeCm;
    float distanceToHomeM;
    uint16_t groundSpeedCmS;
    int16_t directionToHome;
    float accMagnitude;
    bool healthy;
    float errorAngle;
    float gpsDataIntervalSeconds;
    float altitudeDataIntervalSeconds;
    float gpsRescueTaskIntervalSeconds;
    float velocityToHomeCmS;
    float alitutudeStepCm;
    float maxPitchStep;
    float absErrorAngle;
} rescueSensorData_s;

typedef struct {
    rescuePhase_e phase;
    rescueFailureState_e failure;
    rescueSensorData_s sensor;
    rescueIntent_s intent;
    bool isAvailable;
} rescueState_s;

#define GPS_RESCUE_MAX_YAW_RATE          180    // deg/sec max yaw rate
#define GPS_RESCUE_MIN_DESCENT_DIST_M    5      // minimum descent distance
#define GPS_RESCUE_MAX_THROTTLE_ITERM    200    // max iterm value for throttle in degrees * 100

static float rescueThrottle;
static float rescueYaw;
float       gpsRescueAngle[ANGLE_INDEX_COUNT] = { 0, 0 };
bool        magForceDisable = false;
static bool newGPSData = false;
static pt2Filter_t throttleDLpf;
static pt1Filter_t velocityDLpf;
static pt3Filter_t velocityUpsampleLpf;

rescueState_s rescueState;

void gpsRescueInit(void)
{
    rescueState.sensor.gpsRescueTaskIntervalSeconds = HZ_TO_INTERVAL(TASK_GPS_RESCUE_RATE_HZ);

    float cutoffHz, gain;
    cutoffHz = positionConfig()->altitude_d_lpf / 100.0f;
    gain = pt2FilterGain(cutoffHz, rescueState.sensor.gpsRescueTaskIntervalSeconds);
    pt2FilterInit(&throttleDLpf, gain);

    cutoffHz = gpsRescueConfig()->pitchCutoffHz / 100.0f;
    rescueState.intent.velocityPidCutoff = cutoffHz;
    rescueState.intent.velocityPidCutoffModifier = 1.0f;
    gain = pt1FilterGain(cutoffHz, 1.0f);
    pt1FilterInit(&velocityDLpf, gain);

    cutoffHz *= 4.0f; 
    gain = pt3FilterGain(cutoffHz, rescueState.sensor.gpsRescueTaskIntervalSeconds);
    pt3FilterInit(&velocityUpsampleLpf, gain);
}

/*
 If we have new GPS data, update home heading if possible and applicable.
*/
void gpsRescueNewGpsData(void)
{
    newGPSData = true;
}

static void rescueStart(void)
{
    rescueState.phase = RESCUE_INITIALIZE;
}

static void rescueStop(void)
{
    rescueState.phase = RESCUE_IDLE;
}

// Things that need to run when GPS Rescue is enabled, and while armed, but while there is no Rescue in place
static void setReturnAltitude(void)
{
    // Hold maxAltitude at zero while disarmed, but if set_home_point_once is true, hold maxAlt until power cycled
    if (!ARMING_FLAG(ARMED) && !gpsConfig()->gps_set_home_point_once) {
        rescueState.intent.maxAltitudeCm = 0.0f;
        return;
    }

    // While armed, but not during the rescue, update the max altitude value
    rescueState.intent.maxAltitudeCm = fmaxf(rescueState.sensor.currentAltitudeCm, rescueState.intent.maxAltitudeCm);

    if (newGPSData) {
        // set the target altitude to current values, so there will be no D kick on first run
        rescueState.intent.targetAltitudeCm = rescueState.sensor.currentAltitudeCm;

        // Keep the descent distance and intended altitude up to date with latest GPS values
        rescueState.intent.descentDistanceM = constrainf(rescueState.sensor.distanceToHomeM, GPS_RESCUE_MIN_DESCENT_DIST_M, gpsRescueConfig()->descentDistanceM);
        const float initialAltitudeCm = gpsRescueConfig()->initialAltitudeM * 100.0f;
        const float rescueAltitudeBufferCm = gpsRescueConfig()->rescueAltitudeBufferM * 100.0f;
        switch (gpsRescueConfig()->altitudeMode) {
            case GPS_RESCUE_ALT_MODE_FIXED:
                rescueState.intent.returnAltitudeCm = initialAltitudeCm;
                break;
            case GPS_RESCUE_ALT_MODE_CURRENT:
                rescueState.intent.returnAltitudeCm = rescueState.sensor.currentAltitudeCm + rescueAltitudeBufferCm;
                break;
            case GPS_RESCUE_ALT_MODE_MAX:
            default:
                rescueState.intent.returnAltitudeCm = rescueState.intent.maxAltitudeCm + rescueAltitudeBufferCm;
                break;
        }
    }
}

static void rescueAttainPosition(void)
{
    // runs at 100hz, but only updates RPYT settings when new GPS Data arrives and when not in idle phase.
    static float previousVelocityError = 0.0f;
    static float velocityI = 0.0f;
    static float throttleI = 0.0f;
    static float previousAltitudeError = 0.0f;
    static int16_t throttleAdjustment = 0;

    switch (rescueState.phase) {
    case RESCUE_IDLE:
        // values to be returned when no rescue is active
        gpsRescueAngle[AI_PITCH] = 0.0f;
        gpsRescueAngle[AI_ROLL] = 0.0f;
        rescueThrottle = rcCommand[THROTTLE];
        return;
    case RESCUE_INITIALIZE:
        // Initialize internal variables each time GPS Rescue is started
        previousVelocityError = 0.0f;
        velocityI = 0.0f;
        throttleI = 0.0f;
        previousAltitudeError = 0.0f;
        throttleAdjustment = 0;
        rescueState.intent.disarmThreshold = gpsRescueConfig()->disarmThreshold / 10.0f;
        return;
    case RESCUE_DO_NOTHING:
        // 20s of slow descent for switch induced sanity failures to allow time to recover
        gpsRescueAngle[AI_PITCH] = 0.0f;
        gpsRescueAngle[AI_ROLL] = 0.0f;
        rescueThrottle = gpsRescueConfig()->throttleHover - 100;
        return;
     default:
        break;
    }

    /**
        Altitude (throttle) controller
    */
    // currentAltitudeCm is updated at TASK_GPS_RESCUE_RATE_HZ
    const float altitudeError = (rescueState.intent.targetAltitudeCm - rescueState.sensor.currentAltitudeCm) * 0.01f;
    // height above target in metres (negative means too low)
    // at the start, the target starts at current altitude plus one step.  Increases stepwise to intended value.

    // P component
    const float throttleP = gpsRescueConfig()->throttleP * altitudeError;

    // I component
    throttleI += 0.1f * gpsRescueConfig()->throttleI * altitudeError * rescueState.sensor.altitudeDataIntervalSeconds;
    throttleI = constrainf(throttleI, -1.0f * GPS_RESCUE_MAX_THROTTLE_ITERM, 1.0f * GPS_RESCUE_MAX_THROTTLE_ITERM);
    // up to 20% increase in throttle from I alone

    // D component is error based, so includes positive boost when climbing and negative boost on descent
    float verticalSpeed = ((altitudeError - previousAltitudeError) / rescueState.sensor.altitudeDataIntervalSeconds);
    previousAltitudeError = altitudeError;
    verticalSpeed += rescueState.intent.descentRateModifier * verticalSpeed;
    // add up to 2x D when descent rate is faster

    float throttleD = pt2FilterApply(&throttleDLpf, verticalSpeed);

    throttleD = gpsRescueConfig()->throttleD * throttleD;

    // acceleration component not currently implemented - was needed previously due to GPS lag, maybe not needed now.

    float tiltAdjustment = 1.0f - getCosTiltAngle(); // 0 = flat, gets to 0.2 correcting on a windy day
    tiltAdjustment *= (gpsRescueConfig()->throttleHover - 1000);
    // if hover is 1300, and adjustment .2, this gives us 0.2*300 or 60 of extra throttle, not much, but useful
    // too much and landings with lots of pitch adjustment, eg windy days, can be a problem

    throttleAdjustment = throttleP + throttleI + throttleD + tiltAdjustment;

    rescueThrottle = gpsRescueConfig()->throttleHover + throttleAdjustment;
    rescueThrottle = constrainf(rescueThrottle, gpsRescueConfig()->throttleMin, gpsRescueConfig()->throttleMax);
    DEBUG_SET(DEBUG_GPS_RESCUE_THROTTLE_PID, 0, lrintf(throttleP));
    DEBUG_SET(DEBUG_GPS_RESCUE_THROTTLE_PID, 1, lrintf(throttleD));

    /**
        Heading / yaw controller
    */
    // simple yaw P controller with roll mixed in.
    // attitude.values.yaw is set by imuCalculateEstimatedAttitude() and is updated from GPS while groundspeed exceeds 2 m/s
    // below 2m/s groundspeed, the IMU uses gyro to estimate yaw attitude change from previous values
    // above 2m/s, GPS course over ground us ysed to 'correct' the IMU heading
    // if the course over ground, due to wind or pre-exiting movement, is different from the attitude of the quad, the GPS correction will be less accurate
    // the craft should not return much less than 5m/s during the rescue or the GPS corrections may be inaccurate.
    // the faster the return speed, the more accurate the IMU will be, but the consequences of IMU error at the start are greater
    // A compass (magnetometer) is vital for accurate GPS rescue at slow speeds, but must be calibrated and validated
    // WARNING:  Some GPS units give false Home values!  Always check the arrow points to home on leaving home.
    rescueYaw = rescueState.sensor.errorAngle * gpsRescueConfig()->yawP * rescueState.intent.yawAttenuator * 0.1f;
    rescueYaw = constrainf(rescueYaw, -GPS_RESCUE_MAX_YAW_RATE, GPS_RESCUE_MAX_YAW_RATE);
    // rescueYaw is the yaw rate in deg/s to correct the heading error

    // *** mix in some roll.  very important for heading tracking, since a yaw rate means the quad has drifted sideways
    const float rollMixAttenuator = constrainf(1.0f - fabsf(rescueYaw) * 0.01f, 0.0f, 1.0f);
    // less roll at higher yaw rates, no roll at 100 deg/s of yaw
    const float rollAdjustment = -rescueYaw * gpsRescueConfig()->rollMix * rollMixAttenuator;
    // if rollMix = 100, the roll:yaw ratio is 1:1 at small angles, reducing linearly to zero when the yaw rate is 100 deg/s
    // when gpsRescueConfig()->rollMix is zero, there is no roll adjustment
    // rollAdjustment is degrees * 100
    // note that the roll element has the opposite sign to the yaw element *before* GET_DIRECTION
    const float rollLimit = 100.0f * rescueState.intent.rollAngleLimitDeg;
    gpsRescueAngle[AI_ROLL] = constrainf(rollAdjustment, -rollLimit, rollLimit);
    // gpsRescueAngle is added to the normal roll Angle Mode corrections in pid.c

    rescueYaw *= GET_DIRECTION(rcControlsConfig()->yaw_control_reversed);
    // rescueYaw is the yaw rate in deg/s to correct the heading error

    /**
        Pitch / velocity controller
    */
    static float pitchAdjustment = 0.0f;
    if (newGPSData) {

        const float sampleIntervalNormaliseFactor = rescueState.sensor.gpsDataIntervalSeconds * 10.0f;

        const float velocityError = rescueState.intent.targetVelocityCmS - rescueState.sensor.velocityToHomeCmS;
        // velocityError is in cm per second, positive means too slow.
        // NB positive pitch setpoint means nose down.
        // target velocity can be very negative leading to large error before the start, with overshoot

        // P component
        const float velocityP = velocityError * gpsRescueConfig()->velP;

        // I component
        velocityI += 0.01f * gpsRescueConfig()->velI * velocityError * sampleIntervalNormaliseFactor * rescueState.intent.velocityItermRelax;
        // velocityItermRelax is a time-based factor, 0->1 with time constant of 1s from when we start to fly home
        // avoids excess iTerm during the initial acceleration phase.
        velocityI *= rescueState.intent.proximityToLandingArea;
        // reduce iTerm sharply when velocity decreases in landing phase, to minimise overshoot during deceleration

        const float pitchAngleLimit = rescueState.intent.pitchAngleLimitDeg * 100.0f;
        const float velocityPILimit = 0.5f * pitchAngleLimit;
        velocityI = constrainf(velocityI, -velocityPILimit, velocityPILimit);
        // I component alone cannot exceed half the max pitch angle

        // D component
        float velocityD = ((velocityError - previousVelocityError) / sampleIntervalNormaliseFactor);
        previousVelocityError = velocityError;
        velocityD *= gpsRescueConfig()->velD;

        // smooth the D steps
        const float cutoffHz = rescueState.intent.velocityPidCutoff * rescueState.intent.velocityPidCutoffModifier;
        // note that this cutoff is increased up to 2x as we get closer to landing point in descend()
        const float gain = pt1FilterGain(cutoffHz, rescueState.sensor.gpsDataIntervalSeconds);
        pt1FilterUpdateCutoff(&velocityDLpf, gain);
        velocityD = pt1FilterApply(&velocityDLpf, velocityD);

        pitchAdjustment = velocityP + velocityI + velocityD;
        pitchAdjustment = constrainf(pitchAdjustment, -pitchAngleLimit, pitchAngleLimit);
        // limit to maximum allowed angle

        // pitchAdjustment is the absolute Pitch angle adjustment value in degrees * 100
        // it gets added to the normal level mode Pitch adjustments in pid.c
        DEBUG_SET(DEBUG_GPS_RESCUE_VELOCITY, 0, lrintf(velocityP));
        DEBUG_SET(DEBUG_GPS_RESCUE_VELOCITY, 1, lrintf(velocityD));
    }

    // upsampling and smoothing of pitch angle steps
    float pitchAdjustmentFiltered = pt3FilterApply(&velocityUpsampleLpf, pitchAdjustment);

    
    gpsRescueAngle[AI_PITCH] = pitchAdjustmentFiltered;
    // this angle gets added to the normal pitch Angle Mode control values in pid.c - will be seen in pitch setpoint

    DEBUG_SET(DEBUG_GPS_RESCUE_VELOCITY, 3, lrintf(rescueState.intent.targetVelocityCmS));
    DEBUG_SET(DEBUG_GPS_RESCUE_TRACKING, 1, lrintf(rescueState.intent.targetVelocityCmS));
}

static void performSanityChecks(void)
{
    static timeUs_t previousTimeUs = 0; // Last time Stalled/LowSat was checked
    static float prevAltitudeCm = 0.0f; // to calculate ascent or descent change
    static float prevTargetAltitudeCm = 0.0f; // to calculate ascent or descent target change
    static float previousDistanceToHomeCm = 0.0f; // to check that we are returning
    static int8_t secondsLowSats = 0; // Minimum sat detection
    static int8_t secondsDoingNothing; // Limit on doing nothing
    const timeUs_t currentTimeUs = micros();

    if (rescueState.phase == RESCUE_IDLE) {
        rescueState.failure = RESCUE_HEALTHY;
        return;
    } else if (rescueState.phase == RESCUE_INITIALIZE) {
        // Initialize these variables each time a GPS Rescue is started
        previousTimeUs = currentTimeUs;
        prevAltitudeCm = rescueState.sensor.currentAltitudeCm;
        prevTargetAltitudeCm = rescueState.intent.targetAltitudeCm;
        previousDistanceToHomeCm = rescueState.sensor.distanceToHomeCm;
        secondsLowSats = 0;
        secondsDoingNothing = 0;
    }

    // Handle events that set a failure mode to other than healthy.
    // Disarm via Abort when sanity on, or for hard Rx loss in FS_ONLY mode
    // Otherwise allow 20s of semi-controlled descent with impact disarm detection
    const bool hardFailsafe = !rxIsReceivingSignal();

    if (rescueState.failure != RESCUE_HEALTHY) {
        // Default to 20s semi-controlled descent with impact detection, then abort
        rescueState.phase = RESCUE_DO_NOTHING;

        switch(gpsRescueConfig()->sanityChecks) {
        case RESCUE_SANITY_ON:
            rescueState.phase = RESCUE_ABORT;
            break;
        case RESCUE_SANITY_FS_ONLY:
            if (hardFailsafe) {
                rescueState.phase = RESCUE_ABORT;
            }
            break;
        default:
            // even with sanity checks off,
            // override when Allow Arming without Fix is enabled without GPS_FIX_HOME and no Control link available.
            if (gpsRescueConfig()->allowArmingWithoutFix && !STATE(GPS_FIX_HOME) && hardFailsafe) {
                rescueState.phase = RESCUE_ABORT;
            }
        }
    }

    // Crash detection is enabled in all rescues.  If triggered, immediately disarm.
    if (crashRecoveryModeActive()) {
        setArmingDisabled(ARMING_DISABLED_ARM_SWITCH);
        disarm(DISARM_REASON_CRASH_PROTECTION);
        rescueStop();
    }

    // Check if GPS comms are healthy
    // ToDo - check if we have an altitude reading; if we have Baro, we can use Landing mode for controlled descent without GPS
    if (!rescueState.sensor.healthy) {
        rescueState.failure = RESCUE_GPSLOST;
    }

    //  Things that should run at a low refresh rate (such as flyaway detection, etc) will be checked at 1Hz
    const timeDelta_t dTime = cmpTimeUs(currentTimeUs, previousTimeUs);
    if (dTime < 1000000) { //1hz
        return;
    }
    previousTimeUs = currentTimeUs;

    // checks that we are getting closer to home.
    // if the quad is stuck, or if GPS data packets stop, there will be no change in distance to home
    // we can't use rescueState.sensor.currentVelocity because it will be held at the last good value if GPS data updates stop
    if (rescueState.phase == RESCUE_FLY_HOME) {
        const float velocityToHomeCmS = previousDistanceToHomeCm - rescueState.sensor.distanceToHomeCm; // cm/s
        previousDistanceToHomeCm = rescueState.sensor.distanceToHomeCm;
        rescueState.intent.secondsFailing += (velocityToHomeCmS < 0.5f * rescueState.intent.targetVelocityCmS) ? 1 : -1;
        rescueState.intent.secondsFailing = constrain(rescueState.intent.secondsFailing, 0, 15);
        if (rescueState.intent.secondsFailing == 15) {
#ifdef USE_MAG
            //If there is a mag and has not been disabled, we have to assume is healthy and has been used in imu.c
            if (sensors(SENSOR_MAG) && gpsRescueConfig()->useMag && !magForceDisable) {
                //Try again with mag disabled
                magForceDisable = true;
                rescueState.intent.secondsFailing = 0;
            } else
#endif
            {
                rescueState.failure = RESCUE_FLYAWAY;
            }
        }
    }

    secondsLowSats += (!STATE(GPS_FIX) || (gpsSol.numSat < GPS_MIN_SAT_COUNT)) ? 1 : -1;
    secondsLowSats = constrain(secondsLowSats, 0, 10);

    if (secondsLowSats == 10) {
        rescueState.failure = RESCUE_LOWSATS;
    }


    // These conditions ignore sanity mode settings, and apply in all rescues, to handle getting stuck in a climb or descend

    const float actualAltitudeChange = rescueState.sensor.currentAltitudeCm - prevAltitudeCm;
    const float targetAltitudeChange = rescueState.intent.targetAltitudeCm - prevTargetAltitudeCm;
    const float ratio = actualAltitudeChange / targetAltitudeChange;
    prevAltitudeCm = rescueState.sensor.currentAltitudeCm;
    prevTargetAltitudeCm = rescueState.intent.targetAltitudeCm;

    switch (rescueState.phase) {
    case RESCUE_LANDING:
        rescueState.intent.secondsFailing += ratio > 0.5f ? -1 : 1;
        rescueState.intent.secondsFailing = constrain(rescueState.intent.secondsFailing, 0, 10);
        if (rescueState.intent.secondsFailing == 10) {
            rescueState.phase = RESCUE_ABORT;
            // Landing mode shouldn't take more than 10s
        }
        break;
    case RESCUE_ATTAIN_ALT:
    case RESCUE_DESCENT:
        rescueState.intent.secondsFailing += ratio > 0.5f ? -1 : 1;
        rescueState.intent.secondsFailing = constrain(rescueState.intent.secondsFailing, 0, 10);
        if (rescueState.intent.secondsFailing == 10) {
            rescueState.phase = RESCUE_LANDING;
            rescueState.intent.secondsFailing = 0;
            // if can't climb, or slow descending, enable impact detection and time out in 10s
        }
        break;
    case RESCUE_DO_NOTHING:
        secondsDoingNothing = MIN(secondsDoingNothing + 1, 20);
        if (secondsDoingNothing == 20) {
            rescueState.phase = RESCUE_ABORT;
            // time-limited semi-controlled fall with impact detection
        }
        break;
    default:
        // do nothing
        break;
    }

    DEBUG_SET(DEBUG_RTH, 2, (rescueState.failure * 10 + rescueState.phase));
    DEBUG_SET(DEBUG_RTH, 3, (rescueState.intent.secondsFailing * 100 + secondsLowSats));
}

static void sensorUpdate(void)
{
    static float prevDistanceToHomeCm = 0.0f;
    const timeUs_t currentTimeUs = micros();

    static timeUs_t previousAltitudeDataTimeUs = 0;
    const timeDelta_t altitudeDataIntervalUs = cmpTimeUs(currentTimeUs, previousAltitudeDataTimeUs);
    rescueState.sensor.altitudeDataIntervalSeconds = altitudeDataIntervalUs * 0.000001f;
    previousAltitudeDataTimeUs = currentTimeUs;

    rescueState.sensor.currentAltitudeCm = getAltitude();

    DEBUG_SET(DEBUG_GPS_RESCUE_TRACKING, 2, lrintf(rescueState.sensor.currentAltitudeCm));
    DEBUG_SET(DEBUG_GPS_RESCUE_THROTTLE_PID, 2, lrintf(rescueState.sensor.currentAltitudeCm));
    DEBUG_SET(DEBUG_GPS_RESCUE_HEADING, 0, rescueState.sensor.groundSpeedCmS); // groundspeed cm/s
    DEBUG_SET(DEBUG_GPS_RESCUE_HEADING, 1, gpsSol.groundCourse); // degrees * 10
    DEBUG_SET(DEBUG_GPS_RESCUE_HEADING, 2, attitude.values.yaw); // degrees * 10
    DEBUG_SET(DEBUG_GPS_RESCUE_HEADING, 3, rescueState.sensor.directionToHome); // degrees * 10

    rescueState.sensor.healthy = gpsIsHealthy();

    if (rescueState.phase == RESCUE_LANDING) {
        // do this at sensor update rate, not the much slower GPS rate, for quick disarm
        rescueState.sensor.accMagnitude = (float) sqrtf(sq(acc.accADC[Z] - acc.dev.acc_1G) + sq(acc.accADC[X]) + sq(acc.accADC[Y])) * acc.dev.acc_1G_rec;
        // Note: subtracting 1G from Z assumes the quad is 'flat' with respect to the horizon.  A true non-gravity acceleration value, regardless of attitude, may be better.
    }

    rescueState.sensor.directionToHome = GPS_directionToHome;
    rescueState.sensor.errorAngle = (attitude.values.yaw - rescueState.sensor.directionToHome) * 0.1f;
    // both attitude and direction are in degrees * 10, errorAngle is degrees
    if (rescueState.sensor.errorAngle <= -180) {
        rescueState.sensor.errorAngle += 360;
    } else if (rescueState.sensor.errorAngle > 180) {
        rescueState.sensor.errorAngle -= 360;
    }
    rescueState.sensor.absErrorAngle = fabsf(rescueState.sensor.errorAngle);

    if (!newGPSData) {
        return;
        // GPS ground speed, velocity and distance to home will be held at last good values if no new packets
    }

    rescueState.sensor.distanceToHomeCm = GPS_distanceToHomeCm;
    rescueState.sensor.distanceToHomeM = rescueState.sensor.distanceToHomeCm / 100.0f;
    rescueState.sensor.groundSpeedCmS = gpsSol.groundSpeed; // cm/s

    rescueState.sensor.gpsDataIntervalSeconds = getGpsDataIntervalSeconds();
    // Range from 10ms (100hz) to 1000ms (1Hz). Intended to cover common GPS data rates and exclude unusual values.

    rescueState.sensor.velocityToHomeCmS = ((prevDistanceToHomeCm - rescueState.sensor.distanceToHomeCm) / rescueState.sensor.gpsDataIntervalSeconds);
    // positive = towards home.  First value is useless since prevDistanceToHomeCm was zero.
    prevDistanceToHomeCm = rescueState.sensor.distanceToHomeCm;

    DEBUG_SET(DEBUG_GPS_RESCUE_VELOCITY, 2, lrintf(rescueState.sensor.velocityToHomeCmS));
    DEBUG_SET(DEBUG_GPS_RESCUE_TRACKING, 0, lrintf(rescueState.sensor.velocityToHomeCmS));
}

// This function flashes "RESCUE N/A" in the OSD if:
// 1. sensor healthy - GPS data is being received.
// 2. GPS has a 3D fix.
// 3. GPS number of satellites is greater than or equal to the minimum configured satellite count.
// Note 1: cannot arm without the required number of sats
// hence this flashing indicates that after having enough sats, we now have below the minimum and the rescue likely would fail
// Note 2: this function does not take into account the distance from home
// The sanity checks are independent, this just provides the OSD warning
static bool checkGPSRescueIsAvailable(void)
{
    static timeUs_t previousTimeUs = 0; // Last time LowSat was checked
    const timeUs_t currentTimeUs = micros();
    static int8_t secondsLowSats = 0; // Minimum sat detection
    static bool lowsats = false;
    static bool noGPSfix = false;
    bool result = true;

    if (!gpsIsHealthy() || !STATE(GPS_FIX_HOME)) {
        return false;
    }

    //  Things that should run at a low refresh rate >> ~1hz
    const timeDelta_t dTime = cmpTimeUs(currentTimeUs, previousTimeUs);
    if (dTime < 1000000) { //1hz
        if (noGPSfix || lowsats) {
            result = false;
        }
        return result;
    }

    previousTimeUs = currentTimeUs;

    if (!STATE(GPS_FIX)) {
        result = false;
        noGPSfix = true;
    } else {
        noGPSfix = false;
    }

    secondsLowSats = constrain(secondsLowSats + ((gpsSol.numSat < GPS_MIN_SAT_COUNT) ? 1 : -1), 0, 2);
    if (secondsLowSats == 2) {
        lowsats = true;
        result = false;
    } else {
        lowsats = false;
    }

    return result;
}

void disarmOnImpact(void)
{
    if (rescueState.sensor.accMagnitude > rescueState.intent.disarmThreshold) {
        setArmingDisabled(ARMING_DISABLED_ARM_SWITCH);
        disarm(DISARM_REASON_GPS_RESCUE);
        rescueStop();
    }
}

void descend(void)
{
    if (newGPSData) {
        const float distanceToLandingAreaM = rescueState.sensor.distanceToHomeM - (rescueState.intent.targetLandingAltitudeCm / 200.0f);
        // considers home to be a circle half landing height around home to avoid overshooting home point
        rescueState.intent.proximityToLandingArea = constrainf(distanceToLandingAreaM / rescueState.intent.descentDistanceM, 0.0f, 1.0f);
        rescueState.intent.velocityPidCutoffModifier = 2.5f - rescueState.intent.proximityToLandingArea;
         // 1.5 when starting descent, 2.5 when almost landed; multiplier for velocity step cutoff filter
        rescueState.intent.targetVelocityCmS = gpsRescueConfig()->rescueGroundspeed * rescueState.intent.proximityToLandingArea;
        // reduce target velocity as we get closer to home. Zero within 2m of home, reducing risk of overshooting.
        // if quad drifts further than 2m away from home, should by then have rotated towards home, so pitch is allowed
        rescueState.intent.rollAngleLimitDeg = gpsRescueConfig()->maxRescueAngle * rescueState.intent.proximityToLandingArea;
        // reduce roll capability when closer to home, none within final 2m
    }

    // configure altitude step for descent, considering interval between altitude readings
    rescueState.intent.altitudeStep = -1.0f * rescueState.sensor.altitudeDataIntervalSeconds * gpsRescueConfig()->descendRate;

    // descend more slowly if return altitude is less than 20m
    const float descentAttenuator = rescueState.intent.returnAltitudeCm / 2000.0f;
    if (descentAttenuator < 1.0f) {
        rescueState.intent.altitudeStep *= descentAttenuator;
    }
    // descend more quickly from higher altitude
    rescueState.intent.descentRateModifier = constrainf(rescueState.intent.targetAltitudeCm / 5000.0f, 0.0f, 1.0f);
    rescueState.intent.targetAltitudeCm += rescueState.intent.altitudeStep * (1.0f + (2.0f * rescueState.intent.descentRateModifier));
    // increase descent rate to max of 3x default above 50m, 2x above 25m, 1.2 at 5m, default by ground level.
}

void gpsRescueUpdate(void)
// runs at gpsRescueTaskIntervalSeconds, and runs whether or not rescue is active
{
    if (!FLIGHT_MODE(GPS_RESCUE_MODE)) {
        rescueStop(); // sets phase to RESCUE_IDLE; does nothing else.  RESCUE_IDLE tasks still run.
    } else if (FLIGHT_MODE(GPS_RESCUE_MODE) && rescueState.phase == RESCUE_IDLE) {
        rescueStart(); // sets phase to rescue_initialise if we enter GPS Rescue mode while idle
        rescueAttainPosition(); // Initialise basic parameters when a Rescue starts (can't initialise sensor data reliably)
        performSanityChecks(); // Initialises sanity check values when a Rescue starts
    }

    // Will now be in RESCUE_INITIALIZE mode, if just entered Rescue while IDLE, otherwise stays IDLE

    sensorUpdate(); // always get latest GPS and Altitude data, update ascend and descend rates

    static bool initialAltitudeLow = true;
    static bool initialVelocityLow = true;
    rescueState.isAvailable = checkGPSRescueIsAvailable();

    switch (rescueState.phase) {
    case RESCUE_IDLE:
        // in Idle phase = NOT in GPS Rescue
        // update the return altitude and descent distance values, to have valid settings immediately they are needed
        setReturnAltitude();
        break;
        // sanity checks are bypassed in IDLE mode; instead, failure state is always initialised to HEALTHY
        // target altitude is always set to current altitude.

    case RESCUE_INITIALIZE:
        // Things that should be done at the start of a Rescue
        rescueState.intent.targetLandingAltitudeCm = 100.0f * gpsRescueConfig()->targetLandingAltitudeM;
        if (!STATE(GPS_FIX_HOME)) {
            // we didn't get a home point on arming
            rescueState.failure = RESCUE_NO_HOME_POINT;
            // will result in a disarm via the sanity check system, with delay if switch induced
            // alternative is to prevent the rescue by returning to IDLE, but this could cause flyaways
        } else if (rescueState.sensor.distanceToHomeM < gpsRescueConfig()->minRescueDth) {
            if (rescueState.sensor.distanceToHomeM < 5.0f && rescueState.sensor.currentAltitudeCm < rescueState.intent.targetLandingAltitudeCm) {
                // attempted initiation within 5m of home, and 'on the ground' -> instant disarm, for safety reasons
                rescueState.phase = RESCUE_ABORT;
            } else {
                // Otherwise, attempted initiation inside minimum activation distance, at any height -> landing mode
                rescueState.intent.altitudeStep = -rescueState.sensor.altitudeDataIntervalSeconds * gpsRescueConfig()->descendRate;
                rescueState.intent.targetVelocityCmS = 0; // zero forward velocity
                rescueState.intent.pitchAngleLimitDeg = 0; // flat on pitch
                rescueState.intent.rollAngleLimitDeg = 0.0f; // flat on roll also
                rescueState.intent.proximityToLandingArea = 0.0f; // force velocity iTerm to zero
                rescueState.intent.targetAltitudeCm = rescueState.sensor.currentAltitudeCm + rescueState.intent.altitudeStep;
                rescueState.phase = RESCUE_LANDING;
                // start landing from current altitude
            }
        } else {
            rescueState.phase = RESCUE_ATTAIN_ALT;
            rescueState.intent.secondsFailing = 0; // reset the sanity check timer for the climb
            initialAltitudeLow = (rescueState.sensor.currentAltitudeCm < rescueState.intent.returnAltitudeCm);
            rescueState.intent.yawAttenuator = 0.0f;
            rescueState.intent.targetVelocityCmS = rescueState.sensor.velocityToHomeCmS;
            rescueState.intent.pitchAngleLimitDeg = 0.0f; // no pitch
            rescueState.intent.rollAngleLimitDeg = 0.0f; // no roll until flying home
            rescueState.intent.altitudeStep = 0.0f;
            rescueState.intent.descentRateModifier = 0.0f;
            rescueState.intent.velocityPidCutoffModifier = 1.0f; // normal cutoff until descending when increases 150->250% during descent
            rescueState.intent.proximityToLandingArea = 0.0f; // force velocity iTerm to zero
            rescueState.intent.velocityItermRelax = 0.0f; // and don't accumulate any
        }
        break;

    case RESCUE_ATTAIN_ALT:
        // gradually increment the target altitude until the craft reaches target altitude
        // note that this can mean the target altitude may increase above returnAltitude if the craft lags target
        // sanity check will abort if altitude gain is blocked for a cumulative period
        rescueState.intent.altitudeStep = ((initialAltitudeLow) ? gpsRescueConfig()->ascendRate : -1.0f * gpsRescueConfig()->descendRate) * rescueState.sensor.gpsRescueTaskIntervalSeconds;
        const bool currentAltitudeLow = rescueState.sensor.currentAltitudeCm < rescueState.intent.returnAltitudeCm;
        if (initialAltitudeLow == currentAltitudeLow) {
            // we started low, and still are low; also true if we started high, and still are too high
            rescueState.intent.targetAltitudeCm += rescueState.intent.altitudeStep;
        } else {
            // target altitude achieved - move on to ROTATE phase, returning at target altitude
            rescueState.intent.targetAltitudeCm = rescueState.intent.returnAltitudeCm;
            rescueState.intent.altitudeStep = 0.0f;
            rescueState.phase = RESCUE_ROTATE;
        }

        rescueState.intent.targetVelocityCmS = rescueState.sensor.velocityToHomeCmS;
        // gives velocity P and I no error that otherwise would be present due to velocity drift at the start of the rescue
        break;

    case RESCUE_ROTATE:
        if (rescueState.intent.yawAttenuator < 1.0f) { // acquire yaw authority over one second
            rescueState.intent.yawAttenuator += rescueState.sensor.gpsRescueTaskIntervalSeconds;
        }
        if (rescueState.sensor.absErrorAngle < 30.0f) {
            rescueState.intent.pitchAngleLimitDeg = gpsRescueConfig()->maxRescueAngle; // allow pitch
            rescueState.phase = RESCUE_FLY_HOME; // enter fly home phase
            rescueState.intent.secondsFailing = 0; // reset sanity timer for flight home
            rescueState.intent.proximityToLandingArea = 1.0f; // velocity iTerm activated, initialise proximity for descent phase at 1.0
        }
        initialVelocityLow = rescueState.sensor.velocityToHomeCmS < gpsRescueConfig()->rescueGroundspeed; // used to set direction of velocity target change
        rescueState.intent.targetVelocityCmS = rescueState.sensor.velocityToHomeCmS;
        break;

    case RESCUE_FLY_HOME:
        if (rescueState.intent.yawAttenuator < 1.0f) { // be sure to accumulate full yaw authority
            rescueState.intent.yawAttenuator += rescueState.sensor.gpsRescueTaskIntervalSeconds;
        }

        // velocity PIDs are now active
        // update target velocity gradually, aiming for rescueGroundspeed with a time constant of 1.0s
        const float targetVelocityError = gpsRescueConfig()->rescueGroundspeed - rescueState.intent.targetVelocityCmS;
        const float velocityTargetStep = rescueState.sensor.gpsRescueTaskIntervalSeconds * targetVelocityError;
        // velocityTargetStep is positive when starting low, negative when starting high
        const bool targetVelocityIsLow = rescueState.intent.targetVelocityCmS < gpsRescueConfig()->rescueGroundspeed;
        if (initialVelocityLow == targetVelocityIsLow) {
            // also true if started faster than target velocity and target is still high
            rescueState.intent.targetVelocityCmS += velocityTargetStep;
        }

        rescueState.intent.velocityItermRelax += 0.5f * rescueState.sensor.gpsRescueTaskIntervalSeconds * (1.0f - rescueState.intent.velocityItermRelax);
        // slowly introduce velocity iTerm accumulation at start, goes 0 ->1 with time constant 2.0s
        // there is always a lot of lag at the start

        rescueState.intent.velocityPidCutoffModifier = 2.0f - rescueState.intent.velocityItermRelax; 
        // higher velocity cutoff for initial few seconds to improve accuracy; can be smoother later

        rescueState.intent.rollAngleLimitDeg = 0.5f * rescueState.intent.velocityItermRelax * gpsRescueConfig()->maxRescueAngle;
        // gradually gain roll capability to max of half of max pitch angle

        if (newGPSData) {
            if (rescueState.sensor.distanceToHomeM <= rescueState.intent.descentDistanceM) {
                rescueState.phase = RESCUE_DESCENT;
                rescueState.intent.secondsFailing = 0; // reset sanity timer for descent
            }
        }
        break;

    case RESCUE_DESCENT:
        // attenuate velocity and altitude targets while updating the heading to home
        if (rescueState.sensor.currentAltitudeCm < rescueState.intent.targetLandingAltitudeCm) {
            // enter landing mode once below landing altitude
            rescueState.phase = RESCUE_LANDING;
            rescueState.intent.secondsFailing = 0; // reset sanity timer for landing
        }
        descend();
        break;

    case RESCUE_LANDING:
        // Reduce altitude target steadily until impact, then disarm.
        // control yaw angle and throttle and pitch, attenuate velocity, roll and pitch iTerm
        // increase velocity smoothing cutoff as we get closer to ground
        descend();
        disarmOnImpact();
        break;

    case RESCUE_COMPLETE:
        rescueStop();
        break;

    case RESCUE_ABORT:
        setArmingDisabled(ARMING_DISABLED_ARM_SWITCH);
        disarm(DISARM_REASON_FAILSAFE);
        rescueState.intent.secondsFailing = 0; // reset sanity timers so we can re-arm
        rescueStop();
        break;

    case RESCUE_DO_NOTHING:
        disarmOnImpact();
        break;

    default:
        break;
    }

    DEBUG_SET(DEBUG_GPS_RESCUE_TRACKING, 3, lrintf(rescueState.intent.targetAltitudeCm));
    DEBUG_SET(DEBUG_GPS_RESCUE_THROTTLE_PID, 3, lrintf(rescueState.intent.targetAltitudeCm));
    DEBUG_SET(DEBUG_RTH, 0, lrintf(rescueState.intent.maxAltitudeCm));

    performSanityChecks();
    rescueAttainPosition();

    newGPSData = false;
}

float gpsRescueGetYawRate(void)
{
    return rescueYaw;
}

float gpsRescueGetThrottle(void)
{
    // Calculated a desired commanded throttle scaled from 0.0 to 1.0 for use in the mixer.
    // We need to compensate for min_check since the throttle value set by gps rescue
    // is based on the raw rcCommand value commanded by the pilot.
    float commandedThrottle = scaleRangef(rescueThrottle, MAX(rxConfig()->mincheck, PWM_RANGE_MIN), PWM_RANGE_MAX, 0.0f, 1.0f);
    commandedThrottle = constrainf(commandedThrottle, 0.0f, 1.0f);

    return commandedThrottle;
}

bool gpsRescueIsConfigured(void)
{
    return failsafeConfig()->failsafe_procedure == FAILSAFE_PROCEDURE_GPS_RESCUE || isModeActivationConditionPresent(BOXGPSRESCUE);
}

bool gpsRescueIsAvailable(void)
{
    return rescueState.isAvailable;
}

bool gpsRescueIsDisabled(void)
// used for OSD warning
{
    return (!STATE(GPS_FIX_HOME));
}

#ifdef USE_MAG
bool gpsRescueDisableMag(void)
{
    return ((!gpsRescueConfig()->useMag || magForceDisable) && (rescueState.phase >= RESCUE_INITIALIZE) && (rescueState.phase <= RESCUE_LANDING));
}
#endif
#endif
