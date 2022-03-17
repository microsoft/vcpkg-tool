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

  console.log('INSTALL ESPIDF METHOD');
  const pythonPath = activation.findAlias('python');
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
  console.log('ACTIVATE ESPIDF METHOD');

  const pythonPath = activation.findAlias('python');
  if (!pythonPath) {
    throw new Error(i`Python is not installed`);
  }

  const directoryLocation = await isFilePath(targetLocation) ? targetLocation.fsPath : targetLocation.toString();

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
            console.log({envVariable: splitLine[0].trim(), value: splitLine[1].trim()});
            activation.addEnvironmentVariable(splitLine[0].trim(), [splitLine[1].trim()]);
          }
          else {
            console.log('PATH test');
            const pathValues = splitLine[1].split(delimiter);
            for (const path of pathValues) {
              if (path.trim() !== '%PATH%' && path.trim() !== '$PATH') {
                console.log({envVariable: splitLine[0].trim(), value: splitLine[1].trim()});
                activation.addPath(splitLine[0].trim(), session.fileSystem.file(path));
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

  /*
  activation.findAlias;
  activation.addAlias('idf.py', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe ${activation.environment.get('IDF_PATH')}\\tools\\idf.py`);
  activation.tools.set('idf.py', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe ${activation.environment.get('IDF_PATH')}\\tools\\idf.py`);
  activation.tools.set('esptool.py', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe ${activation.environment.get('IDF_PATH')}\\components\\esptool_py\\esptool\\esptool.py`);
  activation.tools.set('espefuse.py', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe ${activation.environment.get('IDF_PATH')}\\components\\esptool_py\\esptool\\espefuse.py`);
  activation.tools.set('otatool.py', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe ${activation.environment.get('IDF_PATH')}\\components\\app_update\\otatool.py`);
  activation.tools.set('parttool.py', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe ${activation.environment.get('IDF_PATH')}\\components\\partition_table\\parttool.py`);
  */

  // does this work? Or are there limitations to overriding system variables
  // activation.tools.set('PYTHON', `${activation.environment.get('IDF_PYTHON_ENV_PATH')}\\Scripts\\python.exe`);

  return true;
}