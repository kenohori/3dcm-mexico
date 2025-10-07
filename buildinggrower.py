import rasterio
import numpy as np
from collections import deque
import matplotlib.pyplot as plt

def region_growing(image, seed_point, threshold):
    rows, cols = image.shape
    region_mask = np.zeros_like(image, dtype=bool)
    
    # Check if seed point is valid
    if not (0 <= seed_point[0] < rows and 0 <= seed_point[1] < cols):
        raise ValueError("Seed point is outside image bounds")
    
    # Initialize queue with seed point
    queue = deque([seed_point])
    region_mask[seed_point] = True
    seed_value = image[seed_point]
    
    # Define connectivity patterns
    neighbors = [(-1, 0), (1, 0), (0, -1), (0, 1)]
                 # (-1, -1), (-1, 1), (1, -1), (1, 1)]
    
    area = 0
    while queue:
        current_point = queue.popleft()
        row, col = current_point
        
        # Check all neighbors
        for dr, dc in neighbors:
            new_row, new_col = row + dr, col + dc
            
            # Check if neighbor is within image bounds
            if (0 <= new_row < rows and 0 <= new_col < cols):
                # Check if pixel is not already in region and meets threshold
                if (not region_mask[new_row, new_col] and 
                    abs(float(image[new_row, new_col]) - float(image[row, col])) <= threshold):
                    
                    region_mask[new_row, new_col] = True
                    area = area + 1
                    queue.append((new_row, new_col))
    
    return region_mask, area

if __name__ == "__main__":
	seed_threshold = 10.0
	minimum_area = 45
	# tolerance = 1.5

	input_file = "/Users/ken/Downloads/3dcm cdmx/cropped/buildings wo roads.tif"
	with rasterio.open(input_file) as src:
		raster_band = src.read(1)
		meta = src.meta.copy()


	seeds = []

	for y in range(len(raster_band)):
		for x in range(len(raster_band[y])):
			if raster_band[y, x] > seed_threshold:
				seeds.append([y, x])


	# print(len(seeds))
	buildings_array = np.zeros_like(raster_band, dtype=np.uint32)

	current_building = 1
	for seed in seeds:
		if buildings_array[seed[0], seed[1]] > 0:
			continue
		if raster_band[seed[0], seed[1]] >= 100.0:
			tolerance = 15.0
		elif raster_band[seed[0], seed[1]] >= 50.0:
			tolerance = 0.75
		else:
			tolerance = 0.75

		segmented_mask, area  = region_growing(raster_band, (seed[0], seed[1]), tolerance)
		# print("Building " + str(current_building) + ": " + str(area))
		if area >= minimum_area:
			buildings_array[segmented_mask] = current_building
			current_building = current_building + 1

	output_file = "/Users/ken/Downloads/buildings.tif"
	meta.update(dtype=buildings_array.dtype, count=1)
	meta['nodata'] = 0

	with rasterio.open(output_file, 'w', **meta) as dst:
	    dst.write(buildings_array, 1)

	print(f"Segmented raster written to: {output_file}")