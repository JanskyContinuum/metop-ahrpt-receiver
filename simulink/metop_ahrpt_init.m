function [in,cfg] = metop_ahrpt_init(inputFile,outputCADU,options)
%METOP_AHRPT_INIT Configure a portable, non-looping Simulink run from t=0.
arguments
    inputFile (1,1) string
    outputCADU (1,1) string = ""
    options.StartTimeSeconds (1,1) double {mustBeNonnegative,mustBeFinite} = 0
    options.StopTime (1,1) double {mustBePositive} = Inf
    options.SoftDecision (1,1) logical = true
    options.ShowScopes (1,1) logical = false
    options.CacheFolder (1,1) string = ""
end
sourceDir=fileparts(mfilename('fullpath')); addpath(sourceDir);
root=fileparts(sourceDir);
if outputCADU=="", outputCADU=fullfile(root,'out','metop.cadu'); end
assert(isfile(inputFile),'metop:Input','CS16 input not found: %s',inputFile);
if ~java.io.File(char(inputFile)).isAbsolute(), inputFile=fullfile(pwd,inputFile); end
if ~java.io.File(char(outputCADU)).isAbsolute(), outputCADU=fullfile(pwd,outputCADU); end
inputFile=string(java.io.File(char(inputFile)).getCanonicalPath());
outputCADU=string(java.io.File(char(outputCADU)).getCanonicalPath());
assert(~strcmpi(inputFile,outputCADU),'metop:Collision','Input and output paths must differ.');
outputDir=fileparts(outputCADU); if ~isfolder(outputDir), mkdir(outputDir); end
cfg=struct('inputFile',inputFile,'inputSampleRate',10e6, ...
    'SamplesPerFrame',92160,'StartTimeSeconds',options.StartTimeSeconds, ...
    'outputCADU',outputCADU, ...
    'SoftDecision',options.SoftDecision);
model='demodulator_metop_r';
open_system(fullfile(sourceDir,[model '.slx']));
in=Simulink.SimulationInput(model);
in=in.setModelParameter('StopTime',num2str(options.StopTime,17));
in=in.setBlockParameter(Simulink.ID.getFullName([model ':39']),'Filename',char(inputFile));
in=in.setBlockParameter(Simulink.ID.getFullName([model ':39']),'StartTimeSeconds',num2str(options.StartTimeSeconds,17));
in=in.setBlockParameter(Simulink.ID.getFullName([model ':52']),'Filename',char(outputCADU));
in=in.setBlockParameter(Simulink.ID.getFullName([model ':78']),'SoftDecision',mat2str(options.SoftDecision));
scopeMode='on'; if options.ShowScopes, scopeMode='off'; end
for sid=54
    in=in.setBlockParameter(Simulink.ID.getFullName(sprintf('%s:%d',model,sid)),'Commented',scopeMode);
end
% Generated products belong outside the source directory.
cacheFolder=options.CacheFolder;
if cacheFolder=="", cacheFolder=fullfile(root,'out','slcache'); end
if ~java.io.File(char(cacheFolder)).isAbsolute(), cacheFolder=fullfile(pwd,cacheFolder); end
Simulink.fileGenControl('set','CacheFolder',char(cacheFolder), ...
    'CodeGenFolder',char(fullfile(cacheFolder,'codegen')),'createDir',true);
addpath(char(cacheFolder),'-begin');
% A legacy kernel in the current directory shadows the configured cache.
assert(~isfile(fullfile(pwd,[model '_cgxe.' mexext])), ...
    'metop:StaleCache','Move the generated *_cgxe MEX out of the current directory and rerun.');
end
