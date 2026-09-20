clc all;
close all;
clear all;

ecg_data=csvread("synthetic_ecg_2000.csv");
Fs=250;
f=ecg_data(:,3);
figure(1),plot(f);

F=fft(f);
Fabs=abs(F);

len=length(f);

freq = Fs*(0:(len-1))/len;

figure(2),plot(freq,Fabs);

%%Power-line Interference Removal.....

wc1 = 0.39*pi;
wc2 = 0.41*pi;

M = 91;
shift = (M-1)/2;

for n = 0:1:M-1
    if n ~= shift
        hds(n+1) = ...
            sin(wc1*(n-shift))/(pi*(n-shift)) ...
            - sin(wc2*(n-shift))/(pi*(n-shift));
    end
endfor

hds(shift+1) = ...
    1 - (wc2-wc1)/pi;

figure(3);
plot(hds);
title("Ideal Band Reject Impulse Response");

for n = 0:1:M-1
    wrect(n+1) = 1;
endfor

figure(4);
plot(wrect);
title("Rectangular Window");

h = hds .* wrect;

figure(5);
plot(h);
title("Band Reject FIR Filter");

y = conv(f,h,'same');

figure(6);
plot(y);
title("Filtered ECG");

f=y;

F=fft(f);
Fabs=abs(F);

len=length(f);

freq = Fs*(0:(len-1))/len;

figure(7),plot(freq,Fabs);

%Base Line  Wander Noise....

wc1_bw = (2 * pi * 0.2 / Fs);
wc2_bw = (2 * pi * 0.5 / Fs);

M_bw = 1501;                     % Higher order required for low frequencies
shift_bw = (M_bw - 1) / 2;
hds_bw = zeros(1, M_bw);

for n = 0:1:M_bw-1
    if n ~= shift_bw
        hds_bw(n+1) = sin(wc1_bw*(n-shift_bw))/(pi*(n-shift_bw)) ...
                    - sin(wc2_bw*(n-shift_bw))/(pi*(n-shift_bw));
    end
endfor

hds_bw(shift_bw+1) = 1 - (wc2_bw - wc1_bw)/pi;

figure(8);
plot(hds_bw);
title('Ideal Baseline Wander Band Reject Impulse Response (0.2 Hz - 0.5 Hz)');

wrect_bw = ones(1, M_bw);

figure(9);
plot(wrect_bw);
title('Rectangular Window (Baseline Wander Filter)');

h_bw = hds_bw .* wrect_bw;

figure(10);
plot(h_bw);
title('Baseline Wander Band Reject FIR Filter');

% Apply convolution to reject 0.2 Hz - 0.5 Hz band
y_final = conv(f, h_bw, 'same');

figure(11);
plot(y_final);
title('Final Filtered ECG (Baseline Wander 0.2-0.5 Hz Rejected)');
xlabel('Time (s)'); ylabel('Amp'); grid on;

% FFT of Final Output
F_final = fft(y_final);
Fabs_final = abs(F_final);
freq_final = Fs*(0:(length(y_final)-1))/length(y_final);

figure(12);
plot(freq_final, Fabs_final);
title('FFT Spectrum of Final ECG Signal');
xlabel('Frequency (Hz)'); ylabel('Magnitude'); grid on;
