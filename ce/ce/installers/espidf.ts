// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { delimiter } from 'path';
import { i } from '../i18n';
import { InstallEvents } from '../interfaces/events';
import { Session } from '../session';
import { execute } from '../util/exec-cmd';
import { isFilePath, Uri } from '../util/uri';
import { Vcpkg } from '../vcpkg';

export async function installEspIdf(session: Session, events: Partial<InstallEvents>, targetLocation: Uri) {
  // create the .espressif folder for the espressif installation
  await targetLocation.createDirectory('.espressif');
  session.activation.addTool('IDF_TOOLS_PATH', targetLocation.join('.espressif').fsPath);

  const vcpkg = new Vcpkg(session);
  const pythonPath = await vcpkg.fetch('python3_with_venv');
  if (!pythonPath) {
    throw new Error(i`Could not activate esp-idf: python was not found.`);
  }

  const directoryLocation = await isFilePath(targetLocation) ? targetLocation.fsPath : targetLocation.toString();

  const extendedEnvironment: NodeJS.ProcessEnv = {
    ... await session.activation.getEnvironmentBlock(),
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

  return installPythonEnv.code === 0;
}

export async function activateEspIdf(session: Session, targetLocation: Uri) {
  const vcpkg = new Vcpkg(session);
  const pythonPath = await vcpkg.fetch('python3_with_venv');
  if (!pythonPath) {
    throw new Error(i`Could not activate esp-idf: python was not found.`);
  }

  const directoryLocation = await isFilePath(targetLocation) ? targetLocation.fsPath : targetLocation.toString();

  const activateIdf = await execute(pythonPath, [
    `${directoryLocation}/tools/idf_tools.py`,
    'export',
    '--format',
    'key-value',
    '--prefer-system'
  ], {
    env: await session.activation.getEnvironmentBlock(),
    onStdOutData: (chunk) => {
      chunk.toString().split('\n').forEach((line: string) => {
        const splitLine = line.split('=');
        if (splitLine[0]) {
          if (splitLine[0] !== 'PATH') {
            session.activation.addEnvironmentVariable(splitLine[0].trim(), [splitLine[1].trim()]);
          }
          else {
            const pathValues = splitLine[1].split(delimiter);
            for (const path of pathValues) {
              if (path.trim() !== '%PATH%' && path.trim() !== '$PATH') {
                // we actually want to use the artifacts we installed, not the ones that are being bundled.
                // when espressif supports artifacts properly, we shouldn't need this filter.
                if (! /\.espressif.tools/ig.exec(path)) {
                  session.activation.addPath(splitLine[0].trim(), session.fileSystem.file(path));
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

  return true;
}
