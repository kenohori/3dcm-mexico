from pathlib import Path

p = Path('/Users/ken/Downloads/3dcm cdmx/dtm')
for folder in p.iterdir():
	processed = True
	if not Path.is_dir(folder):
		continue
	for file in folder.iterdir():
		if 'conjunto_de_datos' in str(file):
			processed = False
		for file in folder.iterdir():
			if 'metadatos' in str(file):
				Path.unlink(file)
			if 'conjunto_de_datos' in str(file):
				file.rename(str(file).replace('conjunto_de_datos\\',''))
	if len(folder.parts[-1]) > 8:
		tile = ''
		for file in folder.iterdir():
			if str(file).endswith('.bil'):
				tile = file.parts[-1][0:8]
		newpath = list(folder.parts[0:-1])
		newpath[0] = ''
		newpath.append(tile)
		folder.rename('/'.join(newpath))