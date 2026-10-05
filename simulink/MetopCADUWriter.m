classdef MetopCADUWriter < matlab.System
    % Exact, MSB-first ASM; confirm 8192-bit spacing before initial lock.
    % Retains randomization and RS parity for the existing C++ decoder.
    properties (Nontunable)
        Filename = 'out/metop.cadu'
    end
    properties (Access=private)
        fid = -1
        buffer = false(0,1)
        locked = false
        totalCount = uint32(0)
    end
    methods
        function obj=MetopCADUWriter(varargin)
            setProperties(obj,nargin,varargin{:});
        end
    end
    methods (Access=protected)
        function setupImpl(obj)
            obj.fid=fopen(obj.Filename,'wb');
            assert(obj.fid>=0,'MetopCADUWriter:Open','Cannot create CADU output.');
        end
        function count=stepImpl(obj,bits)
            obj.buffer=[obj.buffer;logical(bits(:))];
            while numel(obj.buffer)>=32
                if ~obj.locked
                    marks=MetopChannelDecoder.findASM(obj.buffer);
                    first=find(ismember(marks+8192,marks),1);
                    if isempty(first)
                        viable=marks(marks+8192+31>numel(obj.buffer));
                        if isempty(viable)
                            obj.buffer=obj.buffer(max(1,end-30):end);
                        else
                            obj.buffer=obj.buffer(viable(1):end);
                        end
                        break
                    end
                    obj.buffer=obj.buffer(marks(first):end); obj.locked=true;
                end
                if numel(obj.buffer)<8192, break; end
                if isempty(MetopChannelDecoder.findASM(obj.buffer(1:32)))
                    obj.locked=false; continue
                end
                bytes=uint8([128 64 32 16 8 4 2 1]*double(reshape(obj.buffer(1:8192),8,1024)));
                assert(fwrite(obj.fid,bytes,'uint8')==1024,'MetopCADUWriter:Write','CADU write failed.');
                obj.totalCount=obj.totalCount+uint32(1);
                obj.buffer=obj.buffer(8193:end);
            end
            count=obj.totalCount;
        end
        function resetImpl(obj)
            obj.buffer=false(0,1); obj.locked=false; obj.totalCount=uint32(0);
            if obj.fid>=0
                fclose(obj.fid); obj.fid=fopen(obj.Filename,'wb');
                assert(obj.fid>=0,'MetopCADUWriter:Open','Cannot reset CADU output.');
            end
        end
        function releaseImpl(obj)
            if obj.fid>=0, fclose(obj.fid); obj.fid=-1; end
        end
        function flag=isInputSizeMutableImpl(~,~), flag=true; end
        function s=getOutputSizeImpl(~), s=[1 1]; end
        function t=getOutputDataTypeImpl(~), t='uint32'; end
        function c=isOutputComplexImpl(~), c=false; end
        function f=isOutputFixedSizeImpl(~), f=true; end
    end
    methods (Static,Access=protected)
        function mode=getSimulateUsingImpl, mode='Interpreted execution'; end
        function flag=showSimulateUsingImpl, flag=true; end
    end
end

