// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { delimiter } from 'path';
import { Activation } from '../artifacts/activation';
import { log } from '../cli/styling';
import { i } from '../i18n';
import { InstallEvents } from '../interfaces/events';
import { Session } from '../session';
import { execute } from '../util/exec-cmd';
import { isFilePath, Uri } from '../util/uri';


export async function installEspIdf(events: Partial<InstallEvents>, targetLocation: Uri, activation: Activation) {
  // create the .espressif folder for the espressif installation
  await targetLocation.createDirectory('.espressif');

  const pythonPath = activation.tools.get('PYTHON');
  if (!pythonPath) {
    throw new Error(i`Python is not installed`);
  }

  const directoryLocation = await isFilePath(targetLocation) ? targetLocation.fsPath : targetLocation.toString();

  const extendedEnvironment: NodeJS.ProcessEnv = {
    ...activation.environmentBlock,
    IDF_PATH: directoryLocation,
    IDF_TOOLS_PATH: `${directoryLocation}/.espressif`
  };

  const installResult = await execute(pythonPath, [
    `${directoryLocation}/tools/idf_tools.py`,
    'install',
    '--targets=all'
  ], {
    env: extendedEnvironment,
    onStdOutData: (chunk) => {
      const regex = /\s(100)%/;
      chunk.toString().split('\n').forEach((line: string) => {
        const match_array = line.match(regex);
        if (match_array !== null) {
          events.heartbeat?.('Installing espidf');
        }
      });
    }
  });

  if (installResult.code) {
    return false;
  }

  const installPythonEnv = await execute(pythonPath, [
    `${directoryLocation}/tools/idf_tools.py`,
    'install-python-env'
  ], {
    env: extendedEnvironment
  });

  if (installPythonEnv.code) {
    return false;
  }

  // call activate, extrapolate what environment is changed
  // change it in the activation object.

  log('installing espidf commands post-git is implemented, but post activation of the necessary esp-idf path / environment variables is not.');
  return true;
}

export async function activateEspIdf(session: Session, targetLocation: Uri, activation: Activation) {
  const pythonPath = activation.tools.get('PYTHON');
  if (!pythonPath) {
    throw new Error(i`Python is not installed`);
  }

  const directoryLocation = await isFilePath(targetLocation) ? targetLocation.fsPath : targetLocation.toString();

  activation.environment.set('IDF_PATH', [directoryLocation]);
  activation.environment.set('IDF_TOOLS_PATH', [`${directoryLocation}\\.espressif`]);
  const paths = activation.paths.getOrDefault('PATH', []);
  paths.push(targetLocation.join('components', 'esptool_py', 'esptool'));
  paths.push(targetLocation.join('components', 'app_update'));
  paths.push(targetLocation.join('components', 'espcoredump'));
  paths.push(targetLocation.join('components', 'partition_table'));
  paths.push(targetLocation.join('.espressif'));

  const activateIdf = await execute(pythonPath, [
    `${directoryLocation}/tools/idf_tools.py`,
    'export',
    '--format',
    'key-value'
  ], {
    env: activation.environmentBlock,
    onStdOutData: (chunk) => {
      chunk.toString().split('\n').forEach((line: string) => {
        const splitLine = line.split('=');
        if (splitLine[0]) {
          if (splitLine[0] !== 'PATH') {
            activation.environment.set(splitLine[0].trim(), [splitLine[1].trim()]);
          }
          else {
            const pathValues = splitLine[1].split(delimiter);
            const paths = activation.paths.getOrDefault(splitLine[0].trim(), []);
            for (const path of pathValues) {
              if (path.trim() !== '%PATH%' && path.trim() !== '$PATH') {
                paths.push(session.fileSystem.file(path));
              }
            }
          }
        }
        console.log(line);
      });
    }
  });

  if (activateIdf.code) {
    return false;
  }

  activation.tools.set('idf.py', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe ${activation.environment.get('IDF_PATH')}\\tools\\idf.py`);
  activation.tools.set('esptool.py', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe ${activation.environment.get('IDF_PATH')}\\components\\esptool_py\\esptool\\esptool.py`);
  activation.tools.set('espefuse.py', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe ${activation.environment.get('IDF_PATH')}\\components\\esptool_py\\esptool\\espefuse.py`);
  activation.tools.set('otatool.py', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe ${activation.environment.get('IDF_PATH')}\\components\\app_update\\otatool.py`);
  activation.tools.set('parttool.py', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe ${activation.environment.get('IDF_PATH')}\\components\\partition_table\\parttool.py`);


  // does this work? Or are there limitations to overriding system variables
  activation.tools.set('PYTHON', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe`);

  return true;
}