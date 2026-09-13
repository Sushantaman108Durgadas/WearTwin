#include <Wire.h>
#include <MPU6050.h>
#include <math.h>

class KalmanFilter {

private:
    float H[2] = {1.0f, 0.0f};
    float x[2] = {0.0f, 0.0f};
    float F[2][2] = {
        {1.0f, 0.0f},
        {0.0f, 1.0f}
    };
    float P[2][2] = {
        {0.01f, 0.0f},
        {0.0f, 1.0f}
    };
    float Q_angle = 0.001f;
    float Q_bias  = 0.003f;
    float R_measure = 0.03f;

public:

    KalmanFilter() = default;
    void setAngle(float angle)
    {
        x[0] = angle;
    }

    float getAngle()
    {
        return x[0];
    }

    float getBias()
    {
        return x[1];
    }

    float predict(float gyroRate, float dt)
    {
        
        F[0][0] = 1.0f;
        F[0][1] = -dt;
        F[1][0] = 0.0f;
        F[1][1] = 1.0f;

        float rate = gyroRate - x[1];

        x[0] = x[0] + rate * dt;

        float P00 = P[0][0];
        float P01 = P[0][1];
        float P10 = P[1][0];
        float P11 = P[1][1];


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

    float update(float measuredAngle)
    {

        float y = measuredAngle - x[0];

        float S = P[0][0] + R_measure;

        float K0 = P[0][0] / S;
        float K1 = P[1][0] / S;

        x[0] = x[0] + K0 * y;
        x[1] = x[1] + K1 * y;

        float P00_old = P[0][0];
        float P01_old = P[0][1];
        float P10_old = P[1][0];
        float P11_old = P[1][1];


        P[0][0] = P00_old - K0 * P00_old;

        P[0][1] = P01_old - K0 * P01_old;

        P[1][0] = P10_old - K1 * P00_old;

        P[1][1] = P11_old - K1 * P01_old;


        return x[0];
    }
};


class InertialState {

private:

    MPU6050 mpu;


    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;

    unsigned long currentTime;
    unsigned long previousTime;

    float dt;

    KalmanFilter kalman;


public:

    InertialState()
    {
        ax = ay = az = 0;
        gx = gy = gz = 0;

        previousTime = 0;
        currentTime = 0;
        dt = 0.0f;
    }


    bool begin()
    {
        mpu.initialize();

        Serial.println("Establishing connection...");

        if (!mpu.testConnection())
        {
            Serial.println("MPU6050 connection failed!");
            return false;
        }

        Serial.println("MPU6050 connected!");

        previousTime = millis();

        return true;
    }


    void updateTime()
    {
        currentTime = millis();

        dt = (currentTime - previousTime) / 1000.0f;

        previousTime = currentTime;
    }

    void readSensor()
    {
        mpu.getMotion6(
            &ax,
            &ay,
            &az,
            &gx,
            &gy,
            &gz
        );
    }

    float getAccelerometerAngle()
    {
        float angle = atan2(
            (float)ax,
            (float)az
        );

        return angle * 180.0f / PI;
    }


    float getGyroRate()
    {
        return (float)gx / 131.0f;
    }


    float update()
    {
        readSensor();

        updateTime();

        float accelAngle = getAccelerometerAngle();

        float gyroRate = getGyroRate();

        static bool firstRun = true;

        if (firstRun)
        {
            kalman.setAngle(accelAngle);
            firstRun = false;
        }

        kalman.predict(
            gyroRate,
            dt
        );


        float estimatedAngle =
            kalman.update(accelAngle);


        return estimatedAngle;
    }


    float getAngle()
    {
        return kalman.getAngle();
    }


    float getBias()
    {
        return kalman.getBias();
    }


    float getDt()
    {
        return dt;
    }


    int16_t getAx()
    {
        return ax;
    }


    int16_t getAy()
    {
        return ay;
    }


    int16_t getAz()
    {
        return az;
    }


    int16_t getGx()
    {
        return gx;
    }


    int16_t getGy()
    {
        return gy;
    }


    int16_t getGz()
    {
        return gz;
    }
};


InertialState imu;



void setup()
{
    Serial.begin(115200);

    Wire.begin(21, 22);

    if (!imu.begin())
    {
        while (1)
        {
            delay(1000);
        }
    }
}



void loop()
{
    float angle = imu.update();

    Serial.print("Angle: ");
    Serial.print(angle);

    Serial.print(" deg");

    Serial.print(" | Bias: ");
    Serial.print(imu.getBias());

    Serial.print(" | dt: ");
    Serial.println(imu.getDt(), 6);

    delay(10);
}
