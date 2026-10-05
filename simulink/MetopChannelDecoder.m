classdef MetopChannelDecoder < matlab.System
    % Acquire four QPSK phases x two symbol-pair phases using repeated ASM.
    % Once acquired, run one continuous rate-3/4 Viterbi until sync is lost.
    properties (Nontunable)
        SoftDecision = true
        TracebackDepth = 96
        FramePeriod = 92160/10e6
        SearchInterval = 10
    end
    properties (Access=private)
        decoder
        selected = 0
        carry = complex(zeros(0,1,'single'))
        sinceASM = 0
        calls = 0
        tail = false(0,1)
        acquisitions = 0
        epoch = -1
    end
    methods
        function obj=MetopChannelDecoder(varargin)
            setProperties(obj,nargin,varargin{:});
        end
    end
    methods (Access=protected)
        function setupImpl(obj,~,~)
            format='Hard'; if obj.SoftDecision, format='Unquantized'; end
            obj.decoder=comm.ViterbiDecoder(poly2trellis(7,[171 133]), ...
                'InputFormat',format,'PuncturePatternSource','Property', ...
                'PuncturePattern',[1;1;0;1;1;0], ...
                'TracebackDepth',obj.TracebackDepth,'TerminationMethod','Continuous');
        end
        function [bits,status]=stepImpl(obj,z,syncState)
            bits=false(0,1);
            if syncState(3)~=obj.epoch
                obj.loseSync(); obj.epoch=syncState(3);
            end
            obj.calls=obj.calls+1;
            if isempty(z) || ~syncState(2)
                obj.loseSync();
            elseif obj.selected==0
                if mod(obj.calls,obj.SearchInterval)==0
                    for hypothesis=1:8
                        phase=mod(hypothesis-1,4);
                        shift=floor((hypothesis-1)/4);
                        reset(obj.decoder);
                        [v,pending]=metop_demapper(z(1+shift:end),complex(zeros(0,1,'single')),single(1i^phase),obj.SoftDecision);
                        trial=logical(obj.decoder(v));
                        marks=MetopChannelDecoder.findASM(trial);
                        if any(ismember(marks+8192,marks))
                            obj.selected=hypothesis; obj.carry=pending;
                            obj.acquisitions=obj.acquisitions+1;
                            obj.sinceASM=0; bits=trial;
                            obj.tail=trial(max(1,end-30):end);
                            break
                        end
                    end
                end
            else
                phase=mod(obj.selected-1,4);
                [v,obj.carry]=metop_demapper(z,obj.carry,single(1i^phase),obj.SoftDecision);
                bits=logical(obj.decoder(v));
                joined=[obj.tail;bits];
                if isempty(MetopChannelDecoder.findASM(joined))
                    obj.sinceASM=obj.sinceASM+obj.FramePeriod;
                else
                    obj.sinceASM=0;
                end
                obj.tail=joined(max(1,end-30):end);
                if obj.sinceASM>0.25, obj.loseSync(); end
            end
            status=uint32([obj.selected;obj.acquisitions]);
        end
        function loseSync(obj)
            obj.selected=0; obj.carry=complex(zeros(0,1,'single'));
            obj.tail=false(0,1); obj.sinceASM=0;
            reset(obj.decoder);
        end
        function resetImpl(obj)
            obj.loseSync(); obj.calls=0; obj.acquisitions=0; obj.epoch=-1;
        end
        function releaseImpl(obj), release(obj.decoder); end
        function flag=isInputSizeMutableImpl(~,~), flag=true; end
        function [a,b]=getOutputSizeImpl(~), a=[70968 1]; b=[2 1]; end
        function [a,b]=getOutputDataTypeImpl(~), a='logical'; b='uint32'; end
        function [a,b]=isOutputComplexImpl(~), a=false; b=false; end
        function [a,b]=isOutputFixedSizeImpl(~), a=false; b=true; end
    end
    methods (Static)
        function positions=findASM(bits)
            marker='00011010110011111111110000011101';
            positions=strfind(char(double(bits(:).')+48),marker);
        end
    end
    methods (Static,Access=protected)
        function mode=getSimulateUsingImpl, mode='Interpreted execution'; end
    end
end
