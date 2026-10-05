function [values,carry] = metop_demapper(symbols,carry,rotation,soft)
%METOP_DEMAPPER Preserve odd symbols across frames; reorder I1 Q1 Q2 I2.
% carry is empty or the final unpaired symbol, in the unrotated frame.
z=[carry; symbols(:)];
n=2*floor(numel(z)/2);
carry=z(n+1:end);
z=z(1:n)*rotation;
a=z(1:2:end); b=z(2:2:end);
values=reshape([real(a).'; imag(a).'; imag(b).'; real(b).'],[],1);
if ~soft, values=single(values<0); end
end
