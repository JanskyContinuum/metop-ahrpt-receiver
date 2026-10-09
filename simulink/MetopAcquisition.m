classdef MetopAcquisition < matlab.System
    % Small supervisor for the standard reset-on-enable synchronization blocks.
    properties (Nontunable)
        FramePeriod = 92160/10e6
    end
    properties (Access=private)
        contrast = 0
        active = false
        silent = 0
        previousCount = uint32(0)
        attempts = 0
    end
    methods (Access=protected)
        function [enabled,state]=stepImpl(obj,raw,count)
            p=abs(fftshift(fft(raw(1:4096)))).^2;
            inner=mean(p(1558:2540));
            outer=(sum(p(411:820))+sum(p(3278:3687)))/single(820);
            ratio=10*log10(max(inner,eps('single'))/max(outer,eps('single')));
            obj.contrast=.95*obj.contrast+.05*double(ratio);
            if obj.active
                if count==obj.previousCount, obj.silent=obj.silent+obj.FramePeriod;
                else, obj.silent=0; end
                if obj.contrast<2.5 || obj.silent>.5
                    obj.active=false; obj.silent=0;
                end
            elseif obj.contrast>4
                obj.active=true; obj.attempts=obj.attempts+1;
            end
            obj.previousCount=count;
            enabled=obj.active;
            state=[obj.contrast;double(enabled);obj.attempts];
        end
        function resetImpl(obj)
            obj.contrast=0; obj.active=false; obj.silent=0;
            obj.previousCount=uint32(0); obj.attempts=0;
        end
        function [a,b]=getOutputSizeImpl(~), a=[1 1]; b=[3 1]; end
        function [a,b]=getOutputDataTypeImpl(~), a='logical'; b='double'; end
        function [a,b]=isOutputComplexImpl(~), a=false; b=false; end
        function [a,b]=isOutputFixedSizeImpl(~), a=true; b=true; end
    end
end
