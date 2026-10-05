classdef CS16FileSource < matlab.System
    % Streaming signed little-endian I,Q int16 pairs. Never loops at EOF.
    properties (Nontunable)
        Filename = 'Metop_20260731_1022.cs16'
        SampleRate = 10e6
        SamplesPerFrame = 92160
        Normalize = true
        StartTimeSeconds = 0
    end
    properties (Access=private)
        fid = -1
        startByteOffset = 0
        remaining = 0
        totalSamples = 0
    end
    methods
        function obj=CS16FileSource(varargin)
            setProperties(obj,nargin,varargin{:});
        end
    end
    methods (Access=protected)
        function validatePropertiesImpl(obj)
            validateattributes(obj.SampleRate,{'numeric'},{'scalar','real','finite','positive'});
            validateattributes(obj.SamplesPerFrame,{'numeric'},{'scalar','integer','positive'});
            validateattributes(obj.StartTimeSeconds,{'numeric'},{'scalar','real','finite','nonnegative'});
        end
        function setupImpl(obj)
            info=dir(obj.Filename);
            assert(isscalar(info) && ~info.isdir,'CS16FileSource:Open','Input must be a regular CS16 file.');
            assert(mod(info.bytes,4)==0,'CS16FileSource:TruncatedIQ','CS16 length must be divisible by four.');
            obj.totalSamples=info.bytes/4;
            obj.startByteOffset=4*floor(obj.StartTimeSeconds*obj.SampleRate);
            assert(obj.startByteOffset<=info.bytes,'CS16FileSource:Offset','Start offset is beyond EOF.');
            obj.fid=fopen(obj.Filename,'rb','ieee-le');
            assert(obj.fid>=0,'CS16FileSource:Open','Cannot open CS16 file.');
            obj.resetImpl();
        end
        function [y,done]=stepImpl(obj)
            y=complex(zeros(obj.SamplesPerFrame,1,'single'));
            n=min(obj.remaining,obj.SamplesPerFrame);
            if n>0
                [raw,count]=fread(obj.fid,[2 n],'int16=>single');
                assert(count==2*n,'CS16FileSource:Read','Input changed or a read failed before EOF.');
                z=complex(raw(1,:),raw(2,:)).';
                if obj.Normalize, z=z/single(32768); end
                y(1:n)=z;
                obj.remaining=obj.remaining-n;
            end
            % The final valid frame is processed before Stop Simulation acts.
            done=obj.remaining==0;
        end
        function resetImpl(obj)
            if obj.fid>=0
                assert(fseek(obj.fid,obj.startByteOffset,'bof')==0,'CS16FileSource:Seek','Cannot seek input.');
            end
            obj.remaining=obj.totalSamples-obj.startByteOffset/4;
        end
        function releaseImpl(obj)
            if obj.fid>=0, fclose(obj.fid); obj.fid=-1; end
        end
        function [a,b]=getOutputSizeImpl(obj), a=[obj.SamplesPerFrame 1]; b=[1 1]; end
        function [a,b]=getOutputDataTypeImpl(~), a='single'; b='logical'; end
        function [a,b]=isOutputComplexImpl(~), a=true; b=false; end
        function [a,b]=isOutputFixedSizeImpl(~), a=true; b=true; end
        function s=getSampleTimeImpl(obj)
            s=createSampleTime(obj,'Type','Discrete','SampleTime',obj.SamplesPerFrame/obj.SampleRate);
        end
    end
    methods (Static,Access=protected)
        function mode=getSimulateUsingImpl, mode='Interpreted execution'; end
        function flag=showSimulateUsingImpl, flag=true; end
    end
end

