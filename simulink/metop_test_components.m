function metop_test_components
% Contract checks for the file interface and all channel ambiguity hypotheses.
folder=fullfile(fileparts(fileparts(mfilename('fullpath'))),'out','component-tests');
if ~isfolder(folder), mkdir(folder); end
rawfile=fullfile(folder,'source.cs16');
fid=fopen(rawfile,'wb','ieee-le'); fwrite(fid,int16([1 -32768 32767 -2 3 4]),'int16'); fclose(fid);
s=CS16FileSource('Filename',rawfile,'SamplesPerFrame',2);
[a,d]=s(); assert(~d && isequal(a,complex(single([1;32767]),single([-32768;-2]))/32768));
[a,d]=s(); assert(d && a(1)==complex(single(3),single(4))/32768 && a(2)==0);
reset(s); [a,d]=s(); assert(~d && real(a(1))==single(1)/32768); release(s);
s=CS16FileSource('Filename',rawfile,'SamplesPerFrame',3); [~,d]=s(); assert(d); release(s);
s=CS16FileSource('Filename',rawfile,'SamplesPerFrame',2,'StartTimeSeconds',1/10e6);
[a,d]=s(); assert(d && real(a(1))==single(32767)/32768); release(s);
rng(734); bytes=uint8(randi([0 255],1024,12)); bytes(1:4,:)=repmat(uint8([26;207;252;29]),1,12);
bits=reshape(dec2bin(bytes(:),8).'-'0',[],1);
coded=convenc(bits,poly2trellis(7,[171 133]),[1;1;0;1;1;0]);
v=1-2*single(coded); q=reshape(v,4,[]);
symbols=reshape([complex(q(1,:),q(2,:));complex(q(4,:),q(3,:))],[],1);
for soft=[false true]
    for phase=0:3
        for shift=0:1
            channel=MetopChannelDecoder('SoftDecision',soft,'SearchInterval',1);
            filename=fullfile(folder,sprintf('channel-%d-%d-%d.cadu',soft,phase,shift));
            writer=MetopCADUWriter('Filename',filename);
            z=symbols(shift+1:end)*single(1i^phase);
            count=uint32(0);
            for first=1:22001:numel(z)
                decoded=channel(z(first:min(end,first+22000)),[10;1;1]);
                count=writer(decoded);
            end
            release(channel); release(writer);
            assert(count>=9,'metop:Test','Insufficient CADUs for phase/shift test.');
            fid=fopen(filename,'rb'); recovered=fread(fid,[1024 Inf],'*uint8'); fclose(fid);
            for k=1:size(recovered,2)
                assert(any(all(bytes==recovered(:,k),1)),'metop:Test','CADU bits differ from transmitted frame.');
            end
        end
    end
end
% False marker, inserted bit, missing marker, reacquisition and partial EOF.
writer=MetopCADUWriter('Filename',fullfile(folder,'reacquire.cadu'));
marker=bits(1:32);
stream=[marker;false(79,1);bits(1:8192*3);true;bits(8192*3+1:8192*7);bits(1:100)];
for first=1:137:numel(stream)
    count=writer(stream(first:min(end,first+136)));
end
release(writer); assert(count==7,'metop:Test','Writer failed framing recovery or emitted a partial frame.');
fid=fopen(fullfile(folder,'reacquire.cadu'),'rb'); recovered=fread(fid,[1024 Inf],'*uint8'); fclose(fid);
assert(isequal(recovered,bytes(:,1:7)),'metop:Test','Framing recovery changed CADU bytes.');
fprintf('PASS: CS16 format/reset/EOF; 16 hard/soft x phase x puncture cases; exact CADU bytes; framing recovery.\n');
end
