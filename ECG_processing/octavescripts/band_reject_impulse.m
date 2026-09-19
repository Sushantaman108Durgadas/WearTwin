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