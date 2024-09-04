import subprocess   # Import subprocess library for running shell commands
import argparse     # Import argparse library for parsing command line arguments
import json         # Import json library for parsing JSON data
import os           # Import os library for interacting with the operating system
import numpy as np  # Import numpy library for numerical operations 
import matplotlib.pyplot as plt     # Import matplotlib library for plotting  
import re           # Import re library for regular expressions  
import time        # Import time library for time-related functions - used in driver function

# Function to handle user input in CLI
## This function parses the command line arguments for traceroute implementation
## and returns the parsed arguments

def parse_arguments():
    parser = argparse.ArgumentParser(description='Traceroute implementation')

    # General description for each line:
        # First two elements are the short and long versions of the way to refer an argument in command line
        # The type bracket specifies the type of the argument
        # The default bracket specifies the default value of the argument
        # The required bracket specifies whether the argument is required or not
        # The help bracket specifies the help message that will be displayed when the user runs the program with the -h flag
    parser.add_argument('-n', '--num_runs', type=int, default=3, help='Number of times traceroute will run')
    parser.add_argument('-d', '--run_delay', type=int, default=1, help='Number of seconds to wait between runs')
    parser.add_argument('-m', '--max_hops', type=int, default=30, help='Maximum number of hops traceroute will take')
    parser.add_argument('-o', '--output', required=True, help='Name and path of JSON file')
    parser.add_argument('-g', '--graph', required=True, help='Name and path of output PDF file')
    parser.add_argument('-t', '--target', help='Target name or IP address')
    parser.add_argument('--test', help='Directory containing test file')

    args = parser.parse_args()    # Parse the arguments

    if not args.target and not args.test:   # If target host or test directory is not specified
        parse.error('Invalid. Please specify valid target host with -t or test directory --test')
    return args

# Run traceroute: This function runs the traceroute command and returns the output as a list

def run_traceroute(target, max_hops):   # The arguments: target(str): target host name/IP address, max_hops(int): maximum number of hops
    try:
        traceroute_command = ['traceroute', '-m', str(max_hops), target]   # Build the traceroute command with arguments
        
        # Execute the traceroute command and store the output in the result variable
        result = subprocess.run(
            traceroute_command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True
        )
        return result.stdout.splitlines()
    
    except subprocess.CalledProcessError as e:
        print("Error has occured: {e}")    # Print the error message
        return []   # Return an empty list if an error occurs

# Parse traceroute output
## This function parses the output of the traceroute command and extracts hop information.
## It returns a list of dictionaries containing parsed hop information.

'''
def parse_traceroute_output(output):
    hops = []
    latency = re.compile(r'(\d+\.\d+)\s+ms')        # Regular expression to match latency in ms

    for line in output:
        if not line or line.startswith("traceroute"):
            continue
        parts = line.split()

        # Check if the first part is hop number
        try:
            hop_number = int(parts[0])
            hosts = []
            latencies = []

            for part in parts[1:]:
                if re.match(r'\d+\.\d+\.\d+', part):    # Check if IPv4 address
                    hosts.append((part, f"({part})"))
                elif part.startswith('(') and part.endswith(')'):
                    hosts.append((parts[1], part))
                elif latency_match := latency.match(part):
                    latencies.append(float(latency_match.group(1)))

            if latencies:
                hop_info = {
                    'hop': hop_number,
                    'hosts': hosts,
                    'min': np.min(latencies),
                    'max': np.max(latencies),
                    'med': np.median(latencies),
                    'avg': np.mean(latencies)
                }
                hops.append(hop_info)
        except ValueError:
            # If the first part is not a number, skip the line
            print(f"Skipping line: {line}" + " due to unexpected format")
    return hops
'''

def parse_traceroute_output(output):    # The argument: output(list): output of the traceroute command
    hops = []
    latency_regex = re.compile(r'(\d+\.\d+)\s+ms')  # Reg expression to match latency in ms

    #for line in output:
    for i, line in enumerate(output):   # This skips the header and empty lines
        if not line or line.startswith("traceroute"):
            continue

        parts = line.split()
        
        # Extract hostname and IP
        hostname = parts[0]
        ip_match = re.search(r'\((\d+\.\d+\.\d+\.\d+)\)', line)  # Find the IP address within parentheses
        if ip_match:
            ip_address = ip_match.group(1)
        else:
            ip_address = None
        
        # Extract latency values
        latencies = latency_regex.findall(line)
        latencies = [float(latency) for latency in latencies]
        
        # Only add hop info if latencies were found
        if latencies:
            hop_info = {
                'hop': i + 1,       # Store hop number as the index in the list + 1
                'hosts': [(hostname, f"({ip_address})")] if ip_address else [(hostname, '')],
                'latencies': latencies,         # Store all latencies for the hop
                'min': np.min(latencies),
                'max': np.max(latencies),
                'med': np.median(latencies),
                'avg': np.mean(latencies)
            }
            hops.append(hop_info)
    return hops

# Calculate and save latency statistics
## Calculates lantency statistics for each hop
## Returns a list of dictionaries containing the hop statistics
'''
def calculate_latency_stats(hops):
    latency_stats = {}

    # Aggregate latency data for each hop
    for hop in hops:
        hop_num = hop['hop']
        if hop_num not in latency_stats:
            latency_stats[hop_num] = {
                'hosts': hop['hosts'],
                # 'latencies': hop['latencies']
                'latencies': []
            }
        #latency_stats[hop_num]['latencies'].extend([hop['avg']])
        latency_stats[hop_num]['latencies'].extend([hop['latencies']])

    stats = []
    for hop_num, data in latency_stats.items():
        #latencies = np.array(data['latencies'])

        latencies = np.array([item for sublist in data['latencies'] for item in sublist]
                              if any(isinstance(i, list) for i in data['latencies'])
                              else data['latencies'])

        # Ensure latencies is not empty before calculating statistics
        if len(latencies) > 0:
            stats.append({
                'hop': hop_num,
                'hosts': data['hosts'],
                'min': np.min(latencies),
                'max': np.max(latencies),
                'med': np.median(latencies),
                'avg': np.mean(latencies)
            })
    return stats
'''
def calculate_latency_stats(hops):      # The argument: hops(list): list of hop information
    latency_stats = {}

    # Aggregate latency data per hop
    for hop in hops:
        hop_num = hop['hop']
        if hop_num not in latency_stats:
            latency_stats[hop_num] = {
                'hosts': hop['hosts'],
                'latencies': []  # Initialize an empty list to store all latencies for this hop
            }
        latency_stats[hop_num]['latencies'].extend(hop['latencies'])  # Extend with all latencies from the current hop

    stats = []

    for hop_num, data in latency_stats.items():
        # Ensure latencies is not empty before calculating statistics
        if len(data['latencies']) > 0:
            latencies = np.array(data['latencies'])
            stats.append({
                'hop': hop_num,
                'hosts': data['hosts'],
                'latencies': latencies,  # Ensure 'latencies' key is present in the final stats dictionary
                'min': np.min(latencies),
                'max': np.max(latencies),
                'med': np.median(latencies),
                'avg': np.mean(latencies)
            })

    return stats



# Handle pre-generated test files

def load_pregen_test_files(test_dir):
    test_ouput = []
    for filename in os.listdir(test_dir):
        if filename.endswith('.txt') or filename.endswith('.json'):
            with open(os.path.join(test_dir, filename), 'r') as f:  # Open file in read mode, variable 'f' refers to the file
                test_output.append(f.readlines())
    return test_output

# Save output in JSON format

def save_json(stats, output_file):
    test_json_output = []

    # Convert ndarray objects to list for JSON serialization
    for stat in stats:
        if isinstance(stat['latencies'], np.ndarray):
            stat['latencies'] = stat['latencies'].tolist()

    with open(output_file, 'w') as file:
        json.dump(stats, file, indent=2)

# Plot a boxplot using latency statistics

def plot_latency_boxplot(stats, output_graph):
    hops = []
    latencies = []
    for hop_stat in stats:
        hops.append(hop_stat['hop'])
        latencies.append(hop_stat['latencies'])

    # Check if hops and latencies are not empty. If empty, return error
    if not hops or not latencies:
        print("Error: 'hops' or 'latencies' is empty. Please check input data")
        return

    # Replace missing data with placeholder value Nan
    for i in range(len(latencies)):
        if not latencies[i]:
            latencies[i] = [np.nan]

    # REMOVE IN FINAL VERSION
    # Print length of hop and latencies for debugging
    print("Length of hops: ", len(hops))
    print("Length of latencies:", len(latencies))
    

    plt.figure(figsize=(10, 6))
    plt.boxplot(latencies, tick_labels=hops)        # Create a boxplot. Note: changed 'labels' to 'tick_labels' as 'labels' is deprecated
    plt.xlabel('Hop number')
    plt.ylabel('Latency (ms)')
    plt.title('Latency distribution per hop')
    plt.savefig(output_graph)       # Save the plot as a PDF file
    plt.close()

# Driver function

def main():
    args = parse_arguments()

    if args.test:
        test_ouput = load_pregen_test_files(args.test)
        hops_data_list = []
        for output in test_output:
            hop_data = parse_traceroute_output(output)
            hops_data_list.extend(hop_data)
        stats = calculate_latency_stats(hops_data_list)
    else:
        hops_data_list = []
        for _ in range(args.num_runs):
            output = run_traceroute(args.target, args.max_hops)
            hop_data = parse_traceroute_output(output)
            hops_data_list.extend(hop_data)
            if args.run_delay > 0:
                time.sleep(args.run_delay)
        stats = calculate_latency_stats(hops_data_list)

    save_json(stats, args.output)
    plot_latency_boxplot(stats, args.graph)

if __name__ == "__main__":
    main()
