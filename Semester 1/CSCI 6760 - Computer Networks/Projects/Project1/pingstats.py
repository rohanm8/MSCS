import subprocess   # Import the subprocess module - run shell commands
import argparse     # Import the argparse module - parse command line arguments
import json         # Import the json module - save data in JSON format
import os           # Import the os module - interact with the operating system
import re           # Import the re module - regular expressions
import numpy as np  # Import the numpy module - numerical operations
import matplotlib.pyplot as plt     # Import the matplotlib module - plotting
import time        # Import time library for time-related functions - used in driver function

# Function to handle user input in CLI

def parse_arguments():
    parser = argparse.ArgumentParser(description='Ping implementation')   # Create an ArgumentParser object

    parser.add_argument('-d', '--run_delay', type=int, default=1, help='Number of seconds to wait between runs')    # Argument for the delay between runs
    parser.add_argument('-m', '--maxping', type=int, default=5, help='Number of ping attempts')   # Argument for the number of runs
    parser.add_argument('-t', '--target', required=True, help='Target IP address or hostname')  # Argument for the target host
    parser.add_argument('-o', '--output', required=True, help='Name and path of JSON file to save results')  # Argument for the output file
    parser.add_argument('-g', '--graph', required=True, help='Name and path of output PDF file for latency boxplot')    # Argument for the output graph

    args = parser.parse_args()
    return args

# Function to run the ping command and return the output as a list

def run_ping(target, count):
    try:
        ping_command = ['ping', '-c', str(count), target]  # Build the ping command with arguments

        # Execute the ping command and store the output in the result variable
        result = subprocess.run(
            ping_command,
            stdout = subprocess.PIPE,   # Capture the standard output
            stderr = subprocess.PIPE,   # Capture the standard error
            universal_newlines = True   # Convert the output to a string
        )
        return result.stdout.splitlines()

    except subprocess.CalledProcessError as e:
        print(f"Error occured: {e}")
        return[]

# Function to parse ping output

def ping_output(output):
    latencies = []  # Initialize an empty list to store latencies
    packet_loss = 0.0
    latency_pattern =  re.compile(r'time=(\d+\.\d+) ms')   # Regular expression pattern to match latency values
    packetloss_pattern = re.compile(r'(\d+)% packet loss')   # Regular expression pattern to match packet loss values

    for line in output:
        latencymatch = latency_pattern.search(line)   # Search for latency values in the line
        if latencymatch:
            latencies.append(float(latencymatch.group(1)))
        packetlossmatch = packetloss_pattern.search(line)
        if packetlossmatch:
            packet_loss = float(packetlossmatch.group(1))

    return latencies, packet_loss

# Function to save the ping results in a JSON file

def save_ping_results_json(latencies, packet_loss, output_file):
    stats = {
        'avg': np.mean(latencies),
        'latencies': latencies,
        'packet_loss': packet_loss,
        'max': np.max(latencies),
        'min': np.min(latencies)  
    }
    with open(output_file, 'w') as f:
        json.dump(stats, f, indent=2)

# Function to plot a boxplot of the latencies

def plot_latency_boxplot(latencies, graph_output):
    if not latencies:
        print('Error: No latencies to plot')
        return

    plt.figure(figsize=(8, 6))
    plt.boxplot(latencies)
    plt.xlabel('Ping attempt')
    plt.ylabel('Latency (ms)')
    plt.title('Ping Latency Boxplot')
    plt.savefig(graph_output)
    plt.close()

# Driver function

def main():
    args = parse_arguments()    # Parse the command line arguments

    total_latencies = []
    packet_loss = 0.0

    for i in range(args.maxping):   # Perform ping multiple times as per --maxping argument
        print(f"Running ping attempt {i+1} of {args.maxping}")

        output = run_ping(args.target, 1)   # Run one ping per iteration
        latencies, packet_loss = ping_output(output)    # Parse the ping output and extract latencies
        total_latencies.extend(latencies)

        if i < args.maxping - 1:    # Wait for the specified delay between runs
            time.sleep(args.run_delay)
        
        save_ping_results_json(total_latencies, packet_loss, args.output)   # Save the ping results in a JSON file
        plot_latency_boxplot(latencies, args.graph)  # Plot a boxplot of the latencies

if __name__ == '__main__':
    main()