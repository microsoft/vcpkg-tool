// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { delimiter } from 'path';
import { Activation } from '../artifacts/activation';
import { i } from '../i18n';
import { UnpackEvents } from '../interfaces/events';
import { Session } from '../session';
import { execute } from '../util/exec-cmd';
import { Uri } from '../util/uri';
import { vcpkgFetch } from '../vcpkg';

export async function installEspIdf(session: Session, events: Partial<UnpackEvents>, targetLocation: Uri) {
  // check for some file that espressif installs to see if it's installed.
  if (await targetLocation.exists('.espressif')) { return true; }

  // create the .espressif folder for the espressif installation
  const dotEspidf = await targetLocation.createDirectory('.espressif');

  const pythonPath = await vcpkgFetch(session, 'python3_with_venv');
  if (!pythonPath) {
    throw new Error(i`Could not activate esp-idf: python was not found.`);
  }

  const targetDirectory = targetLocation.fsPath;

  const extendedEnvironment: NodeJS.ProcessEnv = {
    ... process.env,
    IDF_PATH: targetDirectory,
    IDF_TOOLS_PATH: dotEspidf.fsPath
  };

  const idfTools = targetLocation.join('tools/idf_tools.py').fsPath;
  session.channels.debug(`Running idf installer ${idfTools}`);

  const installResult = await execute(pythonPath, [
    idfTools,
    'install',
    '--targets=all'
  ], {
    env: extendedEnvironment,
    onStdOutData: (chunk) => {
      session.channels.debug('espidf: ' + chunk);
      const regex = /\s(100)%/;
      chunk.toString().split('\n').forEach((line: string) => {
        const match_array = line.match(regex);
        if (match_array !== null) {
          events.unpackArchiveHeartbeat?.('Installing espidf');
        }
      });
    }
  });

  if (installResult.code) {
    return false;
  }

  const installPythonEnv = await execute(pythonPath, [
    idfTools,
    'install-python-env'
  ], {
    env: extendedEnvironment
  });

  return installPythonEnv.code === 0;
}

export async function activateEspIdf(session: Session, activation: Activation, targetLocation: Uri) {
  const pythonPath = await vcpkgFetch(session, 'python3_with_venv');
  if (!pythonPath) {
    throw new Error(i`Could not activate esp-idf: python was not found.`);
  }

  const targetDirectory = targetLocation.fsPath;
  const dotEspidf = targetLocation.join('.espressif');
  const extendedEnvironment: NodeJS.ProcessEnv = {
    ... process.env,
    IDF_PATH: targetDirectory,
    IDF_TOOLS_PATH: dotEspidf.fsPath
  };

  const activateIdf = await execute(pythonPath, [
    `${targetLocation.fsPath}/tools/idf_tools.py`,
    'export',
    '--format',
    'key-value',
    '--prefer-system'
  ], {
    env: extendedEnvironment,
    onStdOutData: (chunk) => {
      chunk.toString().split('\n').forEach((line: string) => {
        const splitLine = line.split('=');
        if (splitLine[0]) {
          if (splitLine[0] !== 'PATH') {
            activation.addEnvironmentVariable(splitLine[0].trim(), [splitLine[1].trim()]);
          }
          else {
            const pathValues = splitLine[1].split(delimiter);
            for (const path of pathValues) {
              if (path.trim() !== '%PATH%' && path.trim() !== '$PATH') {
                // we actually want to use the artifacts we installed, not the ones that are being bundled.
                // when espressif supports artifacts properly, we shouldn't need this filter.
                if (! /\.espressif.tools/ig.exec(path)) {
                  activation.addPath(splitLine[0].trim(), session.fileSystem.file(path));
                }
              }
            }
          }
        }
      });
    }
  });

  if (activateIdf.code) {
    throw new Error(`Failed to activate esp-idf - ${activateIdf.stderr}`);
  }

  activation.addEnvironmentVariable('IDF_PATH', targetDirectory);
  activation.addTool('IDF_TOOLS_PATH', dotEspidf.fsPath);
  return true;
}
