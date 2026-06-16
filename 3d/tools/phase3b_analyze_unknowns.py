#!/usr/bin/env python3
"""
Phase 3B: Analyze unknown hit spatial distribution.

Usage: python tools/phase3b_analyze_unknowns.py <image_path>

Analyzes captured debug mode 20 frames to quantify and visualize unknown hit distribution.
Red pixels = unknown hits, Green pixels = known hits.
"""

import sys
import os

try:
    import numpy as np
    from PIL import Image
    import matplotlib.pyplot as plt
except ImportError as e:
    print(f"ERROR: Missing required Python package: {e}")
    print("Install dependencies with: pip install -r tools/requirements.txt")
    sys.exit(1)

def analyze_unknown_distribution(image_path):
    """Analyze spatial distribution of unknown vs known hits."""
    
    if not os.path.exists(image_path):
        print(f"ERROR: File not found: {image_path}")
        return False
    
    # Load captured frame
    print(f"Loading image: {image_path}")
    img = Image.open(image_path)
    pixels = np.array(img)
    
    if pixels.ndim != 3 or pixels.shape[2] < 3:
        print("ERROR: Invalid image format (expected RGB)")
        return False
    
    # Count red (unknown) vs green (known) pixels
    red_mask = (pixels[:,:,0] > 200) & (pixels[:,:,1] < 50) & (pixels[:,:,2] < 50)
    green_mask = (pixels[:,:,0] < 50) & (pixels[:,:,1] > 200) & (pixels[:,:,2] < 50)
    
    unknown_count = int(np.sum(red_mask))
    known_count = int(np.sum(green_mask))
    total = unknown_count + known_count
    
    print(f"\n{'='*60}")
    print(f"Phase 3B: Unknown Hit Spatial Distribution Analysis")
    print(f"{'='*60}")
    print(f"Image: {image_path}")
    print(f"Resolution: {pixels.shape[1]}x{pixels.shape[0]}")
    print(f"\nResults:")
    print(f"  Unknown hits (red): {unknown_count:,} ({unknown_count/max(total,1)*100:.1f}%)")
    print(f"  Known hits (green): {known_count:,} ({known_count/max(total,1)*100:.1f}%)")
    print(f"  Total hits: {total:,}")
    
    if total == 0:
        print("\n⚠️  WARNING: No hits detected!")
        return False
    
    # Create output directory
    output_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'phase3b_visual')
    os.makedirs(output_dir, exist_ok=True)
    
    # Create spatial heatmap
    fig, axes = plt.subplots(2, 1, figsize=(12, 10))
    
    axes[0].imshow(red_mask.astype(float), cmap='hot', interpolation='nearest')
    axes[0].set_title('Spatial Distribution of Unknown Hits (Red)', fontsize=14, fontweight='bold')
    axes[0].set_xlabel('Screen X')
    axes[0].set_ylabel('Screen Y')
    
    axes[1].imshow(green_mask.astype(float), cmap='Greens', interpolation='nearest')
    axes[1].set_title('Spatial Distribution of Known Hits (Green)', fontsize=14, fontweight='bold')
    axes[1].set_xlabel('Screen X')
    axes[1].set_ylabel('Screen Y')
    
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'unknown_spatial_heatmap.png')
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"\n✓ Heatmap saved to: {output_path}")
    
    # Interpretation
    print(f"\n{'='*60}")
    print(f"Interpretation:")
    print(f"{'='*60}")
    
    unknown_ratio = unknown_count / total
    
    if unknown_ratio > 0.20:
        print(f"\n⚠️  WARNING: >20% unknown hits ({unknown_ratio*100:.1f}%)")
        print("   This suggests algorithm errors in chart classification.")
        print("   Possible causes:")
        print("   - UDF quality issues")
        print("   - TBN orientation errors")
        print("   - UV mapping bugs")
        print("   RECOMMENDATION: Investigate root cause before proceeding.")
        
    elif unknown_ratio > 0.10:
        print(f"\nℹ️  INFO: 10-20% unknown hits ({unknown_ratio*100:.1f}%)")
        print("   This is likely due to box geometry (short_box/tall_box).")
        print("   Expected behavior if Phase 2C box charts are incomplete.")
        print("   RECOMMENDATION: Verify unknowns align with box positions.")
        
    else:
        print(f"\n✓ GOOD: <10% unknown hits ({unknown_ratio*100:.1f}%)")
        print("   Chart classification appears to be working correctly.")
        print("   Small percentage may be grazing angles or edge cases.")
    
    # Check spatial pattern
    if unknown_count > 0:
        # Find centroid of unknown hits
        ys, xs = np.where(red_mask)
        if len(ys) > 0:
            centroid_y = np.mean(ys)
            centroid_x = np.mean(xs)
            print(f"\nUnknown hit centroid: ({centroid_x:.0f}, {centroid_y:.0f})")
            
            # Check if concentrated in specific region
            height, width = red_mask.shape
            if centroid_y < height * 0.3:
                print("  ⚠️  Unknowns concentrated in upper region (ceiling area?)")
            elif centroid_y > height * 0.7:
                print("  ⚠️  Unknowns concentrated in lower region (floor area?)")
            
            if centroid_x < width * 0.3:
                print("  ⚠️  Unknowns concentrated in left region")
            elif centroid_x > width * 0.7:
                print("  ⚠️  Unknowns concentrated in right region")
    
    plt.show()
    
    return True

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python phase3b_analyze_unknowns.py <image_path>")
        print("\nExample:")
        print("  python tools/phase3b_analyze_unknowns.py tools/phase3b_visual/unknown_distribution_frame5.png")
        sys.exit(1)
    
    success = analyze_unknown_distribution(sys.argv[1])
    sys.exit(0 if success else 1)
