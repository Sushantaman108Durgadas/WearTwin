#include "KalmanFilter.h"


KalmanFilter::KalmanFilter()
{
    // Initial values are already assigned
    // through member initialization.
}


void KalmanFilter::setAngle(float angle)
{
    x[0] = angle;
}


float KalmanFilter::getAngle() const
{
    return x[0];
}


float KalmanFilter::getBias() const
{
    return x[1];
}


float KalmanFilter::predict(float gyroRate, float dt)
{
    // -------------------------------------------------
    // F =
    //
    // [ 1  -dt ]
    // [ 0   1  ]
    // -------------------------------------------------

    F[0][0] = 1.0f;
    F[0][1] = -dt;

    F[1][0] = 0.0f;
    F[1][1] = 1.0f;


    // -------------------------------------------------
    // Correct gyro using estimated bias
    //
    // rate = gyro - bias
    // -------------------------------------------------

    float rate = gyroRate - x[1];


    // -------------------------------------------------
    // Predict state
    //
    // angle = angle + corrected_rate * dt
    // -------------------------------------------------

    x[0] += rate * dt;


    // -------------------------------------------------
    // Save old covariance values
    // -------------------------------------------------

    float P00 = P[0][0];
    float P01 = P[0][1];
    float P10 = P[1][0];
    float P11 = P[1][1];


    // -------------------------------------------------
    // P = F P F^T + Q
    //
    // Expanded manually for a 2x2 matrix.
    // -------------------------------------------------

    P[0][0] =
        P00
        - dt * P10
        - dt * P01
        + dt * dt * P11
        + Q_angle;

    P[0][1] =
        P01
        - dt * P11;

    P[1][0] =
        P10
        - dt * P11;

    P[1][1] =
        P11
        + Q_bias;


    return x[0];
}


float KalmanFilter::update(float measuredAngle)
{
    // -------------------------------------------------
    // Innovation / residual
    //
    // y = z - Hx
    //
    // H = [1 0]
    //
    // therefore:
    //
    // y = measuredAngle - estimatedAngle
    // -------------------------------------------------

    float y = measuredAngle - x[0];


    // -------------------------------------------------
    // Innovation covariance
    //
    // S = HPH^T + R
    //
    // Because H = [1 0]:
    //
    // S = P00 + R
    // -------------------------------------------------

    float S = P[0][0] + R_measure;


    // -------------------------------------------------
    // Kalman gain
    //
    // K = PH^T / S
    //
    // K0 = P00 / S
    // K1 = P10 / S
    // -------------------------------------------------

    float K0 = P[0][0] / S;
    float K1 = P[1][0] / S;


    // -------------------------------------------------
    // Correct state
    // -------------------------------------------------

    x[0] += K0 * y;
    x[1] += K1 * y;


    // -------------------------------------------------
    // Save old P values
    // -------------------------------------------------

    float P00 = P[0][0];
    float P01 = P[0][1];
    float P10 = P[1][0];
    float P11 = P[1][1];


    // -------------------------------------------------
    // P = (I - KH)P
    // -------------------------------------------------

    P[0][0] = P00 - K0 * P00;
    P[0][1] = P01 - K0 * P01;

    P[1][0] = P10 - K1 * P00;
    P[1][1] = P11 - K1 * P01;


    return x[0];
}