#!/usr/bin/python

try:
	import sys
	import os
	import shutil
	import zipfile
	import subprocess
	import tarfile
	
	for a in sys.argv:
		print a
	
	gameName = sys.argv[1]
	binariesPath = sys.argv[2]
	resourcesPath = sys.argv[3]
	configPath = sys.argv[4]
	outputPath = sys.argv[5]
	os.environ['JAVA_HOME'] = sys.argv[6]
	os.environ['ANDROID_HOME'] = sys.argv[7]
	os.environ['EMSCRIPTEN'] = sys.argv[8]
	buildTarget = sys.argv[9]
	
	def patchConfig(filePath):
		with open(configPath, 'rb') as f:
			config = f.read()
		config = config.replace('\r\n', '\n')
		while '#' in config:
			commBegin = config.find('#')
			commEnd = config.find('\n', commBegin)
			config = config[:commBegin] + config[commEnd if commEnd != -1 else len(config):]
		while '\n\n' in config:
			config = config.replace('\n\n', '\n')
		with open(filePath, 'rb') as f:
			file = f.read()
		pos = file.find('###InternalConfig###')
		assert pos != -1
		pos2 = file.find('\0', pos)
		size = int(file[pos + len('###InternalConfig###'):pos2])
		assert len(config) <= size
		size = len(config) + 1
		file = file[0:pos] + config + '\0' + file[pos + size:]
		with open(filePath, 'wb') as f:
			f.write(file)
	
	def patchFile(filePath, textFrom, textTo):
		with open(filePath, 'rb') as f:
			content = f.read()
		content = content.replace(textFrom, textTo)
		with open(filePath, 'wb') as f:
			f.write(content)
	
	def makeZip(name, path):
		zip = zipfile.ZipFile(name, 'w', zipfile.ZIP_DEFLATED)
		for root, dirs, files in os.walk(path):
			for file in files:
				zip.write(os.path.join(root, file), os.path.join(os.path.relpath(root, path), file))
		zip.close()
	
	def makeTar(name, path, mode):
		def filter(tarinfo):
			tarinfo.mode = 0777
			return tarinfo
		tar = tarfile.open(name, mode)
		dir = os.getcwd()
		newDir, folder = os.path.split(path)
		os.chdir(newDir)
		tar.add(folder, filter = filter)
		os.chdir(dir)
		tar.close()
	
	def build():
		gameOutputPath = targetOutputPath + '/' + gameName
		
		if buildTarget == 'Windows':
			# Raw files
			os.makedirs(gameOutputPath)
			shutil.copytree(resourcesPath, gameOutputPath + '/Data')
			shutil.copy(binariesPath + '/Windows/FOnline.exe', gameOutputPath + '/' + gameName + '.exe')
			shutil.copy(binariesPath + '/Windows/FOnline.pdb', gameOutputPath + '/' + gameName + '.pdb')
			patchConfig(gameOutputPath + '/' + gameName + '.exe')
			
			# Patch icon
			if os.name == 'nt':
				icoPath = outputPath + '/Client.ico'
				resHackPath = os.path.dirname(os.path.realpath(__file__)) + '/_other/ReplaceVistaIcon.exe'
				r = subprocess.call([resHackPath, gameOutputPath + '/' + gameName + '.exe', icoPath], shell = True)
				assert r == 0
			
			# Zip
			makeZip(targetOutputPath + '/' + gameName + '.zip', gameOutputPath)
			
			# Installer
			pass
			
			# Update binaries
			binPath = resourcesPath + '/../Binaries'
			if not os.path.exists(binPath):
				os.makedirs(binPath)
			shutil.copy(gameOutputPath + '/' + gameName + '.exe', binPath + '/' + gameName + '.exe')
			shutil.copy(gameOutputPath + '/' + gameName + '.pdb', binPath + '/' + gameName + '.pdb')

		elif buildTarget == 'Linux':
			# Raw files
			os.makedirs(gameOutputPath)
			shutil.copytree(resourcesPath, gameOutputPath + '/Data')
			shutil.copy(binariesPath + '/Linux/FOnline32', gameOutputPath + '/' + gameName + '32')
			shutil.copy(binariesPath + '/Linux/FOnline64', gameOutputPath + '/' + gameName + '64')
			patchConfig(gameOutputPath + '/' + gameName + '32')
			patchConfig(gameOutputPath + '/' + gameName + '64')
			
			# Tar
			makeTar(targetOutputPath + '/' + gameName + '.tar', gameOutputPath, 'w')
			makeTar(targetOutputPath + '/' + gameName + '.tar.gz', gameOutputPath, 'w:gz')

		elif buildTarget == 'Mac':
			# Raw files
			os.makedirs(gameOutputPath)
			shutil.copytree(resourcesPath, gameOutputPath + '/Data')
			shutil.copy(binariesPath + '/Mac/FOnline', gameOutputPath + '/' + gameName)
			patchConfig(gameOutputPath + '/' + gameName)
			
			# Tar
			makeTar(targetOutputPath + '/' + gameName + '.tar', gameOutputPath, 'w')
			makeTar(targetOutputPath + '/' + gameName + '.tar.gz', gameOutputPath, 'w:gz')

		elif buildTarget == 'Android':
			shutil.copytree(binariesPath + '/Android', gameOutputPath)
			patchConfig(gameOutputPath + '/libs/armeabi-v7a/libFOnline.so')
			patchConfig(gameOutputPath + '/libs/x86/libFOnline.so')
			
			# Bundle
			shutil.copytree(resourcesPath, gameOutputPath + '/assets')
			with open(gameOutputPath + '/assets/FilesTree.txt', 'wb') as f:
				f.write('\n'.join(os.listdir(resourcesPath)))
			
			# Pack
			antPath = os.path.dirname(os.path.realpath(__file__)) + '/_ant/bin/ant.bat'
			r = subprocess.call([antPath, '-f', gameOutputPath, 'debug'], shell = True)
			assert r == 0
			shutil.copy(gameOutputPath + '/bin/SDLActivity-debug.apk', targetOutputPath + '/' + gameName + '.apk')

		elif buildTarget == 'Web':
			# Release version
			os.makedirs(gameOutputPath)
			shutil.copy(binariesPath + '/Web/index.html', gameOutputPath + '/index.html')
			shutil.copy(binariesPath + '/Web/FOnline.js', gameOutputPath + '/FOnline.js')
			shutil.copy(binariesPath + '/Web/FOnline.js.mem', gameOutputPath + '/FOnline.js.mem')
			shutil.copy(binariesPath + '/Web/SimpleWebServer.py', gameOutputPath + '/SimpleWebServer.py')
			patchConfig(gameOutputPath + '/FOnline.js.mem')
			
			# Debug version
			shutil.copy(binariesPath + '/Web/index.html', gameOutputPath + '/debug.html')
			shutil.copy(binariesPath + '/Web/FOnline_Debug.js', gameOutputPath + '/FOnline_Debug.js')
			shutil.copy(binariesPath + '/Web/FOnline_Debug.js.mem', gameOutputPath + '/FOnline_Debug.js.mem')
			patchConfig(gameOutputPath + '/FOnline_Debug.js.mem')
			patchFile(gameOutputPath + '/debug.html', 'FOnline.js', 'FOnline_Debug.js')
			
			# Generate resources
			r = subprocess.call(['python', os.environ['EMSCRIPTEN'] + '/tools/file_packager.py', \
					'Resources.data', '--preload', resourcesPath + '@/Data', '--js-output=Resources.js'], shell = True)
			assert r == 0
			shutil.move('Resources.js', gameOutputPath + '/Resources.js')
			shutil.move('Resources.data', gameOutputPath + '/Resources.data')
			
			# Patch *.html
			patchFile(gameOutputPath + '/index.html', '$TITLE$', gameName)
			patchFile(gameOutputPath + '/index.html', '$LOADING$', gameName)
			patchFile(gameOutputPath + '/debug.html', '$TITLE$', gameName + ' Debug')
			patchFile(gameOutputPath + '/debug.html', '$LOADING$', gameName + ' Debug')

		else:
			assert False, 'Unknown build target'

	try:
		targetOutputPath = outputPath + '/' + buildTarget
		shutil.rmtree(targetOutputPath, True)
		os.makedirs(targetOutputPath)
		
		build()
	except:
		if targetOutputPath:
			shutil.rmtree(targetOutputPath, True)
		raise

except Exception, e:
	print 'Unhandled exception:'
	print e
	exit(-1)
