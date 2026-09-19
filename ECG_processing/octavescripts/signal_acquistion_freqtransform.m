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
