classdef MetopMonitor < matlab.System
    % Bounded, frame-level physical-layer diagnostics; never stores a pass in RAM.
    properties (Nontunable)
        Filename = 'out/physical.csv'
        FramePeriod = 92160/10e6
        StartTimeSeconds = 0
    end
    properties (Access=private)
        fid = -1
        frame = 0
        snapshots = struct('seconds',{},'raw',{},'agc',{},'rrc',{},'coarse',{},'timing',{},'carrier',{})
        lastSnapshot = -1
    end
    methods (Access=protected)
        function setupImpl(obj)
            obj.fid = fopen(obj.Filename,'w');
            assert(obj.fid>=0,'MetopMonitor:Open','Cannot open diagnostics.');
            fprintf(obj.fid,'seconds,raw_power,agc_power,rrc_power,coarse_power,timing_power,carrier_power,symbols,qpsk_coherence,evm,coarse_hz,contrast_db,signal_present,physical_attempts,channel_hypothesis,channel_acquisitions,cadus\n');
        end
        function stepImpl(obj,raw,agc,rrc,coarse,timing,carrier,freq,syncState,channelState,cadus)
            % An enabled subsystem holds its last outputs while disabled.
            % Exclude those held values from measurements of emitted symbols.
            if ~syncState(2)
                coarse=complex(zeros(0,1,'single'));
                timing=complex(zeros(0,1,'single'));
                carrier=complex(zeros(0,1,'single'));
            end
            if mod(obj.frame,10)==0
                % Sample each stage uniformly to keep monitoring inexpensive.
                power=zeros(1,6); data={raw,agc,rrc,coarse,timing,carrier};
                for k=1:6
                    z=data{k}; z=z(1:max(1,floor(numel(z)/2048)):end);
                    power(k)=mean(abs(z).^2);
                end
                z=carrier(1:max(1,floor(numel(carrier)/4096)):end);
                if isempty(z)
                    coherence=0; evm=NaN;
                else
                    coherence=abs(mean((z./max(abs(z),eps('single'))).^4));
                    q=complex(sign(real(z)),sign(imag(z)));
                    scale=mean(real(z.*conj(q)))/2;
                    evm=sqrt(mean(abs(z-scale*q).^2)/max(mean(abs(scale*q).^2),eps));
                end
                fprintf(obj.fid,'%.9f,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%d,%.6f,%.6f,%.3f,%.4f,%d,%d,%d,%d,%d\n', ...
                    obj.StartTimeSeconds+obj.frame*obj.FramePeriod,power,numel(carrier),coherence,evm,double(freq(1)),syncState,double(channelState),double(cadus));
            end
            bucket=floor(obj.frame*obj.FramePeriod/50);
            if bucket>obj.lastSnapshot
                obj.lastSnapshot=bucket;
                snap.seconds=obj.StartTimeSeconds+obj.frame*obj.FramePeriod;
                names={'raw','agc','rrc','coarse','timing','carrier'};
                data={raw,agc,rrc,coarse,timing,carrier};
                for k=1:6, snap.(names{k})=data{k}(max(1,end-8191):end); end
                obj.snapshots(end+1)=snap;
            end
            obj.frame=obj.frame+1;
        end
        function releaseImpl(obj)
            if obj.fid>=0, fclose(obj.fid); obj.fid=-1; end
            snapshots=obj.snapshots; save([obj.Filename '.snapshots.mat'],'snapshots');
        end
        function flag=isInputSizeMutableImpl(~,~), flag=true; end
        function n=getNumOutputsImpl(~), n=0; end
    end
    methods (Static,Access=protected)
        function mode=getSimulateUsingImpl, mode='Interpreted execution'; end
    end
end
