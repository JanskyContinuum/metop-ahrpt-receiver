function report = metop_compare_simplification(beforeCADU,beforeCSV,afterCADU,afterValidation)
%METOP_COMPARE_SIMPLIFICATION Compare the legacy monitor with native test logs.
% CSV cadence is every ten source frames. Compare all recorded controls and
% CADU bytes; do not infer unobserved transitions between those CSV samples.
before=readtable(beforeCSV);
after=load(afterValidation,'validation_logs','report');
[aTime,a]=rows(after.validation_logs.get('acquisition').Values);
[cTime,c]=rows(after.validation_logs.get('channel').Values);
[nTime,n]=rows(after.validation_logs.get('cadus').Values);
indices=round(before.seconds/(92160/10e6))+1;
assert(all(indices>=1 & indices<=numel(aTime)));
assert(max(abs(aTime(indices)-before.seconds))<1e-8,'metop:Time','Acquisition time differs.');
assert(max(abs(cTime(indices)-before.seconds))<1e-8,'metop:Time','Channel time differs.');
assert(max(abs(nTime(indices)-before.seconds))<1e-8,'metop:Time','CADU time differs.');
assert(max(abs(a(indices,1)-before.contrast_db))<=0.000051, ...
    'metop:Acquisition','Spectral contrast differs beyond legacy CSV rounding.');
assert(isequal(a(indices,2),before.signal_present),'metop:Acquisition','Enable flags differ.');
assert(isequal(a(indices,3),before.physical_attempts),'metop:Acquisition','Attempts differ.');
assert(isequal(double(c(indices,1)),before.channel_hypothesis),'metop:Channel','Hypotheses differ.');
assert(isequal(double(c(indices,2)),before.channel_acquisitions),'metop:Channel','Acquisitions differ.');
assert(isequal(double(n(indices,1)),before.cadus),'metop:CADU','Counts differ.');
f1=fopen(beforeCADU,'rb'); assert(f1>=0); cleanup1=onCleanup(@()fclose(f1));
f2=fopen(afterCADU,'rb'); assert(f2>=0); cleanup2=onCleanup(@()fclose(f2));
bytes=0;
while true
    b1=fread(f1,1024*1024,'*uint8'); b2=fread(f2,1024*1024,'*uint8');
    assert(isequal(b1,b2),'metop:Bytes','CADU bytes differ at or after offset %d.',bytes);
    if isempty(b1),break,end
    bytes=bytes+numel(b1);
end
report=struct('bytesEqual',bytes,'cadus',bytes/1024,'controlSamplesCompared',height(before), ...
    'physicalAttempts',a(end,3),'channelAcquisitions',double(c(end,2)), ...
    'finalTime',after.report.finalTime,'stopEvent',after.report.stopEvent);
disp(report);
fprintf('PASS: byte-identical CADUs and all %d sampled control records match.\n',height(before));
end

function [t,d]=rows(ts)
t=ts.Time;
if size(ts.Data,1)==numel(t), d=reshape(ts.Data,numel(t),[]);
else, d=reshape(ts.Data,[],numel(t)).';
end
end
