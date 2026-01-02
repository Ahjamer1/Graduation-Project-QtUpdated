%% Plot Throughput vs. Load (Averaged per X-Value)
% 1. Reads all raw data points from files
% 2. Groups data by Load (X-axis)
% 3. Calculates the AVERAGE Throughput (Y-axis) for each unique Load
% 4. Plots the single averaged line

clc; clear; close all;

% ====== 1. SETUP PATHS ======
baseDir = 'D:\ElectricalEngineering\#0 Graduation project\The Project\afterUpdate\Graduation-Project-QtUpdated\AllOnSameFigure';
dirLoad = fullfile(baseDir, 'LOAD');
dirTp   = fullfile(baseDir, 'ThroughputPerTimeSlot');
outputDir = fullfile(baseDir, 'output_plots');

if ~exist(outputDir, 'dir'); mkdir(outputDir); end

% ====== 2. READ & COLLECT ALL DATA ======
filesLoad = dir(fullfile(dirLoad, '*.txt'));
filesTp   = dir(fullfile(dirTp, '*.txt'));

N = min(numel(filesLoad), numel(filesTp));
if N == 0; error('No files found.'); end

all_Load = [];
all_Tp   = [];

fprintf('Reading %d file pairs...\n', N);

for k = 1:N
    % Read Load
    dataL = load(fullfile(dirLoad, filesLoad(k).name));
    % Read Throughput
    dataT = load(fullfile(dirTp, filesTp(k).name));
    
    % Ensure column vectors
    dataL = dataL(:);
    dataT = dataT(:);
    
    % Match lengths if necessary
    minLen = min(length(dataL), length(dataT));
    
    % Append to master lists
    all_Load = [all_Load; dataL(1:minLen)];
    all_Tp   = [all_Tp;   dataT(1:minLen)];
end

% ====== 3. GROUP AND AVERAGE (The New Step) ======
% We round to 4 decimal places to ensure that 0.1000001 is treated as 0.1
rounded_Load = round(all_Load, 4);

% Get the unique X values (Load) sorted automatically
unique_Loads = unique(rounded_Load);

% Pre-allocate Y array
avg_Throughput = zeros(size(unique_Loads));

% Calculate average Y for each unique X
for i = 1:length(unique_Loads)
    % Find indices where Load equals the current unique value
    indices = (rounded_Load == unique_Loads(i));
    
    % Average the Throughput values at these indices
    avg_Throughput(i) = mean(all_Tp(indices));
end

% ====== 4. PLOT ======
figHandle = figure('Color', 'w');
hold on;

% Plot the averaged points connected by a line
plot(unique_Loads, avg_Throughput, '-s', ...
    'Color', [0, 0, 1], ...      % Blue Line
    'LineWidth', 2.0, ...        % Thick line
    'MarkerSize', 8, ...         % Visible square markers
    'MarkerEdgeColor', 'b', ...  
    'MarkerFaceColor', 'b');     % Filled Blue

% --- Formatting ---
grid on;
box on;
set(gca, 'FontSize', 10);

xlabel('Offered Load', 'FontSize', 12, 'FontWeight', 'bold');
ylabel('Average Throughput (pkt/slot)', 'FontSize', 12, 'FontWeight', 'bold');
title('Throughput vs. Load', 'FontSize', 14, 'FontWeight', 'bold');

% Optional: Set limits
ylim([0, max(avg_Throughput)*1.1]);

legend('Proposed Scheme', 'Location', 'southeast');
hold off;

% ====== 5. SAVE ======
saveName = 'Throughput_vs_Load_Averaged_Line';
saveas(figHandle, fullfile(outputDir, [saveName '.png']));
savefig(figHandle, fullfile(outputDir, [saveName '.fig']));

fprintf('Done! Consolidated %d raw points into %d averaged points.\n', length(all_Load), length(unique_Loads));
fprintf('Saved to: %s\n', fullfile(outputDir, [saveName '.png']));