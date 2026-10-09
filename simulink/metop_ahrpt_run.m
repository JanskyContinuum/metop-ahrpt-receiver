function result = metop_ahrpt_run(inputFile,outputCADU,varargin)
%METOP_AHRPT_RUN Run to EOF and save a compact, auditable result beside CADU.
if nargin<2, outputCADU=""; end
[in,cfg]=metop_ahrpt_init(inputFile,outputCADU,varargin{:});
raw=dir(cfg.inputFile);
timer=tic; out=sim(in); elapsed=toc(timer);
stream=dir(cfg.outputCADU);
execution=out.SimulationMetadata.ExecutionInfo;
% Keep JSON free of Simulink BlockPath/diagnostic objects.
executionSummary=struct('StopEvent',char(execution.StopEvent), ...
    'StopEventDescription',char(execution.StopEventDescription));
result=struct('configuration',cfg,'rawBytes',raw.bytes, ...
    'rawDurationSeconds',raw.bytes/(4*cfg.inputSampleRate), ...
    'wallSeconds',elapsed,'caduBytes',stream.bytes,'cadus',stream.bytes/1024, ...
    'execution',executionSummary);
assert(mod(stream.bytes,1024)==0,'metop:Alignment','Partial CADU output.');
save(cfg.outputCADU+".result.mat",'result');
fid=fopen(cfg.outputCADU+".result.json",'w');
assert(fid>=0,'metop:Report','Cannot create run report.');
cleanup=onCleanup(@()fclose(fid));
fprintf(fid,'%s\n',jsonencode(result,PrettyPrint=true));
fprintf('CADUs: %d; bytes: %d; wall time: %.1f s\n',result.cadus,result.caduBytes,elapsed);
fprintf('Stop event: %s\n',result.execution.StopEvent);
end
