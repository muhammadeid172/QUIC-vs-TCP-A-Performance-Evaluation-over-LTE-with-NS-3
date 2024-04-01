#!/bin/zsh

# Check if the number of iterations is passed as an argument
if [ -z "$1" ]; then
  echo "Usage: $0 <number of iterations>"
  exit 1
fi

# Initialize sum and n
sum=0
n=$1
distance=$2

# Check if n is a positive integer
if ! [[ "$n" =~ ^[0-9]+$ ]]; then
  echo "Error: The number of iterations must be a positive integer."
  exit 1
fi

for i in $(seq 1 $n); do
  # Execute the command and capture its output
  output=$(./ns3 run Throughput-TCP-over-LTE -- --distance=$distance)

  # Check if output is a valid number
  if [[ $output =~ ^-?[0-9]+(\.[0-9]+)?$ ]]; then
    # Use bc to add the output to sum, handling floating-point numbers
    sum=$(echo "$sum + $output" | bc)
  else
    echo "Error: Program output is not a valid number. Received program output:"
    echo $output
    exit 1
  fi
done

# Calculate the average using bc for floating-point division
average=$(echo "scale=2; $sum / $n" | bc)

echo "Average TCP Throughput [$n simulations, distance = $distance (m)]: $average Mbps."
