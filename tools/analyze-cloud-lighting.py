"""Report cloud-response curves from the current C++ defaults without compiling shaders."""

import math
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_defaults():
    source = (ROOT / "src/Features/PhysicalSky/Ndf.h").read_text(encoding="utf-8")
    body = source.split("struct CloudLightingSettings", 1)[1].split("};", 1)[0]
    return {name: float(value) for name, value in
            re.findall(r"float\s+(\w+)\s*=\s*([\d.]+)f\s*;", body)}


def hg(cosine, g):
    return (1 - g*g) / (4 * math.pi * (1 + g*g - 2*g*cosine)**1.5)


def main():
    settings = read_defaults()
    low_scale = settings["brightness"] * settings["lowBrightness"]
    cirrus_scale = settings["brightness"] * settings["cirrusBrightness"] * 0.5
    print(f"Uploaded brightness scales: low={low_scale:g}, cirrus={cirrus_scale:g}")
    print("Angular solar response (sr^-1)")
    print("degrees,primary,secondary,total,low_scaled,cirrus_scaled")
    for angle in (0, 15, 30, 60, 90, 120, 180):
        mu = math.cos(math.radians(angle))
        first = hg(mu, settings["phasePrimaryG"]) * settings["phasePrimaryIntensity"]
        second = hg(mu, settings["phaseSecondaryG"]) * settings["phaseSecondaryIntensity"]
        total = first + second
        print(f"{angle},{first:.6f},{second:.6f},{total:.6f},"
              f"{total*low_scale:.6f},{total*cirrus_scale:.6f}")

    depth_power = settings["scatterVolumeDepthPower"]
    print("\nOptical response")
    print("q,soft_base,volume_transmission,ambient_broad_transmission")
    for q in (0, 5, 10, 20, 39.9, 40, 40.1, 80, 160):
        print(f"{q:g},{max(0, 1-0.025*q):.6f},"
              f"{math.exp(-q*depth_power):.6f},{math.exp(-q*0.03):.6f}")
    print(f"Volume e-folding q: {1/depth_power:.6f}")

    print("\nHomogeneous local probe estimate above normalized height 0.3")
    print("light_density_per_m,side_q,forward_q_4_probes,forward_q_10_probes")
    for density in (0.05, 0.1, 0.25, 0.5):
        def optical_depth(mu, count):
            spacing = 120 / count
            return settings["sunExtinction"] * density * sum(
                (1+mu) * spacing * 0.45 * (1+(i+0.5)*0.4*mu)
                for i in range(count))
        print(f"{density},{optical_depth(0,4):.6f},"
              f"{optical_depth(1,4):.6f},{optical_depth(1,10):.6f}")

    print("\nHeight profile")
    print("height,volume_height,ambient_height_at_product_0.5")
    for height in (0, 0.05, 0.1, 0.25, 0.5, 1):
        ambient_height = (settings["ambientBase"] + height * (1-settings["ambientBase"])) ** (0.5**0.3*1.8+0.2)
        print(f"{height},{max(height,1e-8)**settings['scatterVolumeHeightPower']:.6f},"
              f"{ambient_height:.6f}")

    print("\nAmbient empty-profile response")
    print("eroded_profile,response")
    for profile in (0, 0.1, 0.5, 0.9, 0.99, 1):
        print(f"{profile},{(1-profile)**settings['ambientProfilePower']:.6f}")

    print("\nCombined ambient response at height=0.5, product=0.5, q=20")
    print("eroded_profile,unfloored_response,low_scaled_response")
    height, product, q = 0.5, 0.5, 20
    ambient_height = (settings["ambientBase"] + height * (1-settings["ambientBase"])) ** (product**0.3*1.8+0.2)
    broad = math.exp(-q*0.03)
    for profile in (0, 0.1, 0.5, 0.9, 0.99, 1):
        response = ambient_height * 1.68 * (1-math.sqrt(product))
        response *= (1-broad)*height**0.25 + broad
        response *= (1-profile)**settings["ambientProfilePower"]
        scaled = low_scale * settings["ambientStrength"] * max(settings["ambientFloor"], response)
        print(f"{profile},{response:.6f},{scaled:.6f}")

    print("\nThese curves describe model components, not measured cloud luminance or a transport solution.")


if __name__ == "__main__":
    main()
