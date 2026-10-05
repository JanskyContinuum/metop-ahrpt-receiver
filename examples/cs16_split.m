% Skrypt do bezpiecznego podziału gigantycznych plików .cs16
% Dzieli plik na równe części, nie przerywając par (I, Q) i oszczędzając RAM.

clear; clc;

% --- KONFIGURACJA ---
input_filename = 'Metop_20260730_1042.cs16'; % <-- WPISZ TUTAJ NAZWĘ SWOJEGO PLIKU 14GB
num_parts = 4;                            % Na ile części podzielić
buffer_size_pairs = 10 * 1024 * 1024;     % Bufor: 10 milionów par (ok. 40 MB w RAM)
% --------------------

% Sprawdzenie czy plik istnieje i pobranie jego rozmiaru
file_info = dir(input_filename);
if isempty(file_info)
error('Nie znaleziono pliku wejściowego. Sprawdź nazwę!');
end

total_bytes = file_info.bytes;
total_samples = floor(total_bytes / 2); % Każda próbka ma 2 bajty (int16)
total_pairs = floor(total_samples / 2); % Każda para to (I, Q)

fprintf('Rozmiar pliku: %.2f GB\n', total_bytes / 1024^3);
fprintf('Całkowita liczba par (I,Q): %d\n', total_pairs);

% Obliczanie ile par trafi do każdej z pierwszych N-1 części
pairs_per_part = floor(total_pairs / num_parts);

% Otwarcie pliku źródłowego do odczytu
fid_in = fopen(input_filename, 'r');
if fid_in == -1
error('Nie można otworzyć pliku źródłowego.');
end

% Główna pętla dzieląca
for i = 1:num_parts
% Generowanie nazwy dla aktualnego kawałka
[filepath, name, ext] = fileparts(input_filename);
out_filename = sprintf('%s_czesc_%d%s', name, i, ext);

fid_out = fopen(out_filename, 'w');
if fid_out == -1
    fclose(fid_in);
    error('Nie można utworzyć pliku wyjściowego: %s', out_filename);
end

% Ostatnia część bierze wszystko co zostało na wypadek reszty z dzielenia
if i == num_parts
    pairs_to_write = total_pairs - (num_parts - 1) * pairs_per_part;
else
    pairs_to_write = pairs_per_part;
end

fprintf('Tworzenie %s... (%.2f GB) - ', out_filename, (pairs_to_write * 4) / 1024^3);

pairs_written = 0;

% Zapis strumieniowy z użyciem bufora
while pairs_written < pairs_to_write
    % Oblicz ile par możemy przeczytać w tym kroku
    pairs_to_read = min(buffer_size_pairs, pairs_to_write - pairs_written);
    samples_to_read = pairs_to_read * 2; % *2 bo I oraz Q

    % Używamy '*int16', aby MATLAB trzymał to w RAM jako int16, a nie double (oszczędność pamięci)
    data = fread(fid_in, samples_to_read, '*int16');

    % Zapisz bezpośrednio do nowego pliku
    fwrite(fid_out, data, 'int16');

    pairs_written = pairs_written + pairs_to_read;
end

fclose(fid_out);
fprintf('Zrobione!\n');


end

fclose(fid_in);
fprintf('\nPodział zakończony sukcesem. Powstało %d nowych plików.\n', num_parts);
