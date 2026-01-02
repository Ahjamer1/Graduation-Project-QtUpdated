%% Dynamic plotting: N metric files -> N Separate Figures with Average Line
% Reads ALL .txt files in a folder
% Plots each on a SEPARATE figure with custom colors and width
% Calculates and plots the AVERAGE line for each file
% Saves each result individually into 'output_plots'
clc; clear; close all;

% ====== SET FOLDER PATH ======
dataDir = 'D:\ElectricalEngineering\#0 Graduation project\The Project\afterUpdate\Graduation-Project-QtUpdated\AllOnSameFigure\ThroughputPerTimeSlotForEachSUType';

% Define output directory inside the data directory
outputDir = fullfile(dataDir, 'output_plots');

% Create the directory if it does not exist
if ~exist(outputDir, 'dir')
    mkdir(outputDir);
    fprintf('Created output directory: %s\n', outputDir);
else
    fprintf('Saving to existing directory: %s\n', outputDir);
end

files = dir(fullfile(dataDir,'*.txt'));
N = numel(files);

if N == 0
    error('No .txt files found in the specified folder.');
end

% --- DEFINE CUSTOM STYLES ---
% Define colors (Blue, Red, Green)
customColors = {
    [0, 0, 1],   % Blue
    [1, 0, 0],   % Red
    [0, 1, 0]    % Green
};

% Define linewidths. 
customLineWidths = {
    1.5,  % Standard width for Blue
    3.0,  % Thicker width for Red
    1.5   % Standard width for Green
};

%Start Loop
for k = 1:N
    filePath = fullfile(dataDir, files(k).name);
    fid = fopen(filePath,'r');
    if fid < 0
        warning('Cannot open file: %s', filePath);
        continue;
    end
    y = fscanf(fid,'%f');
    fclose(fid);
    
    [~, nameBase, ~] = fileparts(files(k).name);
    
    % --- 1. CREATE NEW FIGURE FOR THIS FILE ---
    % 'Visible', 'on' makes it pop up. 
    figHandle = figure('Color','w', 'Visible', 'on', 'Name', nameBase); 
    grid on;
    box on;
    
    % Select a color and linewidth based on the file index k
    currentColor = customColors{mod(k-1, length(customColors)) + 1};
    currentLineWidth = customLineWidths{mod(k-1, length(customLineWidths)) + 1};
    
    % --- 2. PLOT DATA ---
    plot(1:numel(y), y, ...
         'Color', currentColor, ...
         'LineWidth', currentLineWidth, ...
         'DisplayName', 'Throughput'); % Label for legend
     
    hold on; % Keep the current plot to add the average line
    
    % --- 3. CALCULATE AND PLOT AVERAGE ---
    avgVal = mean(y);
    
    % Plot a horizontal dashed line for the average
    % We use [1, numel(y)] for x-range and [avgVal, avgVal] for y-range
    plot([1, numel(y)], [avgVal, avgVal], ...
         'Color', 'k', ...          % Black color for average
         'LineStyle', '--', ...     % Dashed line
         'LineWidth', 2.0, ...
         'DisplayName', sprintf('Average: %.4f', avgVal)); 
    
    hold off; % Release the plot
    
    % --- 4. FORMATTING (Applied to this specific figure) ---
    % Using the filename as the title so you know which plot is which
    title(nameBase, 'Interpreter', 'none', 'FontWeight', 'bold');
    xlabel('Time Slot');
    ylabel('Throughput');
    
    % Add Legend to show what the lines are
    legend('show', 'Location', 'best');
    
    % Set Y-axis limits to 0-1 (Adjust if your average > 1)
    ylim([0, 1]);
    
    % --- 5. SAVE INDIVIDUAL FIGURE ---
    % 1. Save as PNG Image using the filename
    pngName = fullfile(outputDir, [nameBase '.png']);
    saveas(figHandle, pngName);
    
    % 2. Save as MATLAB .fig file (Optional)
    % figName = fullfile(outputDir, [nameBase '.fig']);
    % savefig(figHandle, figName);
    
    fprintf('Processed and saved: %s (Avg: %.4f)\n', nameBase, avgVal);
end
fprintf('All files processed. Check the "%s" folder.\n', outputDir);