import sys
import matplotlib.pyplot as plt
import scienceplots


def read_scale():
    # with open("/sys/bus/event_source/devices/power/events/energy-pkg.scale", "r") as f:
    #     line = f.readline()
    #     return float(line)
    return 2.3e-10


def plot_power_graph(sched_type, interval):
    scale = read_scale()
    file = sched_type + ".csv"
    with open(file, "r") as f:
        lines = f.readlines()

        timesteps = list(range(0, len(lines) * interval, interval))

        sys_power = [
            float(line.split(",")[0]) / float(line.split(",")[1]) for line in lines
        ]
        proc1_power = [
            float(line.split(",")[2]) / float(line.split(",")[1]) for line in lines
        ]
        proc2_power = [
            float(line.split(",")[4]) / float(line.split(",")[1]) for line in lines
        ]

        sys_power = [p * scale * 1000 * 1000 * 1000 for p in sys_power]
        proc1_power = [p * scale * 1000 * 1000 * 1000 for p in proc1_power]
        proc2_power = [p * scale * 1000 * 1000 * 1000 for p in proc2_power]

        plt.plot(timesteps, sys_power, label="system")
        plt.plot(timesteps, proc1_power, label="process 1")
        plt.plot(timesteps, proc2_power, label="process 2")
        plt.ylim(0, 17)
        plt.xlabel("Time (ms)")
        plt.ylabel("Power (Watts)")
        plt.legend()

        plt.savefig(sched_type + "_power_graph.png")
        plt.clf()


def plot_energy_graph(sched_type, interval):
    scale = read_scale()
    file = sched_type + ".csv"
    with open(file, "r") as f:
        lines = f.readlines()

        timesteps = list(range(0, len(lines) * interval, interval))

        sys_energy = [float(line.split(",")[0]) for line in lines]
        proc1_energy = [float(line.split(",")[2]) for line in lines]
        proc2_energy = [float(line.split(",")[4]) for line in lines]

        sys_energy = [p * scale for p in sys_energy]
        proc1_energy = [p * scale for p in proc1_energy]
        proc2_energy = [p * scale for p in proc2_energy]

        plt.ylim(0, 16)
        plt.plot(timesteps, sys_energy, label="system")
        plt.plot(timesteps, proc1_energy, label="process 1")
        plt.plot(timesteps, proc2_energy, label="process 2")

        plt.xlabel("Time (ms)")
        plt.ylabel("Energy (Joules)")
        plt.legend()

        plt.savefig(sched_type + "_energy_graph.png")
        plt.clf()


def plot_timeshare_graph(sched_type, interval):
    scale = read_scale()
    file = sched_type + ".csv"
    with open(file, "r") as f:
        lines = f.readlines()

        timesteps = list(range(0, len(lines) * interval, interval))

        sys_timeshare = [
            (float(line.split(",")[3]) + float(line.split(",")[5]))
            / float(line.split(",")[1])
            for line in lines
        ]
        proc1_timeshare = [
            float(line.split(",")[3]) / float(line.split(",")[1]) for line in lines
        ]
        proc2_timeshare = [
            float(line.split(",")[5]) / float(line.split(",")[1]) for line in lines
        ]

        plt.ylim(0, 20)
        plt.plot(timesteps, sys_timeshare, label="process 1 + process 2")
        plt.plot(timesteps, proc1_timeshare, label="process 1")
        plt.plot(timesteps, proc2_timeshare, label="process 2")
        plt.ylim(0, 1.1)
        plt.xlabel("Time (ms)")
        plt.ylabel(r"CPU time (\%)")
        plt.legend()

        plt.savefig(sched_type + "_timeshare_graph.png")
        plt.clf()


def plot_energy_share_graph(sched_type, interval):
    scale = read_scale()
    file = sched_type + ".csv"
    with open(file, "r") as f:
        lines = f.readlines()

        timesteps = list(range(0, len(lines) * interval, interval))

        proc1_energy_share = [
            float(line.split(",")[2]) / float(line.split(",")[0]) for line in lines
        ]
        proc2_energy_share = [
            float(line.split(",")[4]) / float(line.split(",")[0]) for line in lines
        ]

        # plt.ylim(0, 20)
        plt.plot(timesteps, proc1_energy_share, label="process 1")
        plt.plot(timesteps, proc2_energy_share, label="process 2")
        plt.ylim(0, 1.1)
        plt.xlabel("Time (ms)")
        plt.ylabel(r"Energy share (\%)")
        plt.legend()

        plt.savefig(sched_type + "_energy_share_graph.png")
        plt.clf()


if __name__ == "__main__":
    plt.style.use("science")

    # Increase the figure size
    plt.figure(figsize=(4, 3), dpi=300)

    plot_power_graph(sys.argv[1], int(sys.argv[2]))
    plot_energy_graph(sys.argv[1], int(sys.argv[2]))
    plot_timeshare_graph(sys.argv[1], int(sys.argv[2]))
    plot_energy_share_graph(sys.argv[1], int(sys.argv[2]))
