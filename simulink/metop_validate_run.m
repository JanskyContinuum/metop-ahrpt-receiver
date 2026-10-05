function report = metop_validate_run(inputFile,outputCADU,varargin)
%METOP_VALIDATE_RUN Full-file regression capture with temporary signal logging.
% No monitoring blocks are added and the model file is not saved.
[in,cfg]=metop_ahrpt_init(inputFile,outputCADU,varargin{:});
assert(cfg.StartTimeSeconds==0,'metop:Validation','Validation must start at zero.');
raw=dir(cfg.inputFile);
duration=raw.bytes/(4*cfg.inputSampleRate);
model='demodulator_metop_r';
ids=[90 52 39 78]; ports=[2 1 2 2];
names={'acquisition','cadus','eof','channel'};
paths=cell(size(ids)); original=cell(size(ids));
for k=1:numel(ids)
    paths{k}=Simulink.ID.getFullName(sprintf('%s:%d',model,ids(k)));
    ph=get_param(paths{k},'PortHandles');
    original{k}=get_param(ph.Outport(ports(k)),'DataLogging');
end
cleanup=onCleanup(@()restore_marks(paths,ports,original));
mi=Simulink.SimulationData.ModelLoggingInfo(model);
for k=1:numel(ids)
    Simulink.sdi.markSignalForStreaming(paths{k},ports(k),'on');
    info=Simulink.SimulationData.SignalLoggingInfo(paths{k},ports(k));
    info.LoggingInfo.DataLogging=true;
    info.LoggingInfo.NameMode=1;
    info.LoggingInfo.LoggingName=names{k};
    mi.Signals(k)=info;
end
in=in.setModelParameter('StopTime',num2str(ceil(duration)+1), ...
    'SignalLogging','on','SignalLoggingName','validation_logs', ...
    'DataLoggingOverride',mi,'SaveTime','on','TimeSaveName','tout', ...
    'ReturnWorkspaceOutputs','on');
timer=tic; out=sim(in); elapsed=toc(timer);
validation_logs=out.validation_logs;
stream=dir(cfg.outputCADU);
expectedEnd=(ceil(raw.bytes/4/cfg.SamplesPerFrame)-1)*cfg.SamplesPerFrame/cfg.inputSampleRate;
report=struct('rawBytes',raw.bytes,'rawDurationSeconds',duration, ...
    'wallSeconds',elapsed,'caduBytes',stream.bytes,'cadus',stream.bytes/1024, ...
    'stopEvent',char(out.SimulationMetadata.ExecutionInfo.StopEvent), ...
    'finalTime',out.tout(end),'expectedFinalTime',expectedEnd);
save(cfg.outputCADU+".validation.mat",'validation_logs','report');
fid=fopen(cfg.outputCADU+".validation.json",'w');
assert(fid>=0,'metop:Report','Cannot open validation report.');
fileCleanup=onCleanup(@()fclose(fid));
fprintf(fid,'%s\n',jsonencode(report,PrettyPrint=true));
assert(abs(report.finalTime-expectedEnd)<1e-8,'metop:EOF','Unexpected final frame time.');
assert(strcmp(report.stopEvent,'ModelStop'),'metop:EOF','Did not stop by EOF block.');
assert(validation_logs.get('eof').Values.Data(end),'metop:EOF','Final EOF flag is false.');
assert(mod(stream.bytes,1024)==0,'metop:Alignment','Partial CADU output.');
assert(report.cadus>0,'metop:NoCADUs','No CADUs recovered.');
assert(double(validation_logs.get('cadus').Values.Data(end))==report.cadus, ...
    'metop:Count','Logged CADU count differs from file size.');
fprintf('PASS: zero-offset full run, EOF, %d CADUs, %.6f s final frame.\n',report.cadus,report.finalTime);
end

function restore_marks(paths,ports,original)
for k=1:numel(paths)
    Simulink.sdi.markSignalForStreaming(paths{k},ports(k),original{k});
end
end
