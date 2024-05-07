import sys

import matplotlib.pyplot as plt

def read_scale():
    with open("/sys/bus/event_source/devices/power/events/energy-pkg.scale", "r") as f:
        line = f.readline()
        return float(line)

def plot_power_graph(sched_type, interval):
    scale = read_scale()
    file = sched_type + ".csv"
    with open(file, "r") as f:
        lines = f.readlines()
    
        timesteps = list(range(0, len(lines) * interval, interval))

        sys_power   = [float(line.split(",")[0]) / float(line.split(",")[1]) for line in lines] 
        proc1_power = [float(line.split(",")[2]) / float(line.split(",")[1]) for line in lines]
        proc2_power = [float(line.split(",")[4]) / float(line.split(",")[1]) for line in lines]

        sys_power   = [p * scale * 1000 * 1000 * 1000 for p in sys_power] 
        proc1_power = [p * scale * 1000 * 1000 * 1000 for p in proc1_power]
        proc2_power = [p * scale * 1000 * 1000 * 1000 for p in proc2_power]

        plt.plot(timesteps, sys_power, label='system power')
        plt.plot(timesteps, proc1_power, label='process 1 power')
        plt.plot(timesteps, proc2_power, label='process 2 power')

        plt.xlabel('Time (ms)')
        plt.ylabel('Power (Watts)')
        plt.legend()

        plt.savefig(sched_type + 'power_graph.png')

def plot_energy_graph(sched_type, interval):
    scale = read_scale()
    file = sched_type + ".csv"
    with open(file, "r") as f:
        lines = f.readlines()
    
        timesteps = list(range(0, len(lines) * interval, interval))

        sys_energy   = [float(line.split(",")[0]) for line in lines] 
        proc1_energy = [float(line.split(",")[2]) for line in lines]
        proc2_energy = [float(line.split(",")[4]) for line in lines]
        
        sys_energy   = [p * scale for p in sys_energy] 
        proc1_energy = [p * scale for p in proc1_energy]
        proc2_energy = [p * scale for p in proc2_energy]

        plt.plot(timesteps, sys_energy, label='system energy')
        plt.plot(timesteps, proc1_energy, label='process 1 energy')
        plt.plot(timesteps, proc2_energy, label='process 2 energy')

        plt.xlabel('Time (ms)')
        plt.ylabel('Energy (Joules)')
        plt.legend()

        plt.savefig(sched_type + 'energy_graph.png')


if __name__ == "__main__":
    plot_power_graph(sys.argv[1], int(sys.argv[2]))
    plot_energy_graph(sys.argv[1], int(sys.argv[2]))
