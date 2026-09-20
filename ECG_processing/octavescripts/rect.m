for n = 0:1:M-1
    wrect(n+1) = 1;
endfor

figure(4);
plot(wrect);
title("Rectangular Window");
