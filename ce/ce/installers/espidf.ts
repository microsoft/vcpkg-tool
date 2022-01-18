// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Activation } from '../artifacts/activation';
import { log } from '../cli/styling';
import { i } from '../i18n';
import { execute } from '../util/exec-cmd';
import { Uri } from '../util/uri';


async function installEspIdf(targetLocation: Uri, activation: Activation, options: { subdirectory?: string } = {}) {
  // create the .espressif folder for the espressif installation
  await targetLocation.createDirectory('.espressif');

  const pythonPath = activation.tools.get('python');
  if (!pythonPath) {
    throw new Error(i`Python is not installed`);
  }

  const directoryLocation = `${targetLocation.fsPath.toString()}/${options.subdirectory ?? ''}`;

  // TODO: look into making sure idf_tools.py updates the system's python installation
  // with the required modules.
  const extendedEnvironment: NodeJS.ProcessEnv = {
    ...activation.environmentBlock,
    IDF_PATH: directoryLocation,
    IDF_TOOLS_PATH: `${targetLocation.fsPath.toString()}/.espressif`
  };


  const command = [
    `python.exe ${directoryLocation}/tools/idf_tools.py`, 'install --targets=all',
    `&& python.exe ${directoryLocation}/tools/idf_tools.py install-python-env`];

  const installResult = await execute(pythonPath, [
    `${directoryLocation}/tools/idf_tools.py`,
    'install',
    '--targets=all'
  ],
  {
    env: extendedEnvironment
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

  log('installing espidf commands post-git is implemented, but post activation of the necessary esp-idf path / environment variables is not.');
  return true;
}