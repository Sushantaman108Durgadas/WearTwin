#ifndef KALMAN_FILTER_H
#define KALMAN_FILTER_H

class KalmanFilter {

private:

    // Measurement matrix:
    // H = [1 0]
    float H[2] = {1.0f, 0.0f};

    // State:
    // x[0] = angle
    // x[1] = gyro bias
    float x[2] = {0.0f, 0.0f};

    // State transition matrix
    float F[2][2] = {
        {1.0f, 0.0f},
        {0.0f, 1.0f}
    };

    // State covariance matrix
    float P[2][2] = {
        {0.01f, 0.0f},
        {0.0f, 1.0f}
    };

    // Process noise
    float Q_angle = 0.001f;
    float Q_bias = 0.003f;

    // Measurement noise
    float R_measure = 0.03f;

public:

    KalmanFilter();

    void setAngle(float angle);

    float predict(float gyroRate, float dt);

    float update(float measuredAngle);

    float getAngle() const;

    float getBias() const;
};

#endif