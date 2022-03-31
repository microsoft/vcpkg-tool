// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { delimiter } from 'path';
import { i } from '../i18n';
import { InstallEvents } from '../interfaces/events';
import { Session } from '../session';
import { execute } from '../util/exec-cmd';
import { Uri } from '../util/uri';

export async function installEspIdf(session: Session, events: Partial<InstallEvents>, targetLocation: Uri) {
  // create the .espressif folder for the espressif installation
  const espressifFolder = await targetLocation.createDirectory('.espressif');

  const pythonPath = await session.activation.getAlias('python');
  if (!pythonPath) {
    throw new Error(i`Python is not installed`);
  }

  const installResult = await execute(pythonPath, [
    targetLocation.join('tools', 'idf_tools.py').fsPath,
    'install',
    '--targets=all'
  ], {
    env: await session.activation.getEnvironmentBlock(),
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

  session.channels.verbose(JSON.stringify(installResult, null, 2));

  if (installResult.code) {
    // on failure, clean up the .espressif folder
    await espressifFolder.delete({ recursive: true });
    session.channels.error(installResult.stderr);
    throw new Error(i`There was a failure during the installation of the .espressif tools.`);
  }

  const installPythonEnv = await execute(pythonPath, [
    targetLocation.join('tools', 'idf_tools.py').fsPath,
    'install-python-env'
  ], {
    env: await session.activation.getEnvironmentBlock()
  });

  session.channels.verbose(JSON.stringify(installPythonEnv, null, 2));

  if (installPythonEnv.code) {
    // on failure, clean up the .espressif folder
    await espressifFolder.delete({ recursive: true });
    session.channels.error(installPythonEnv.stderr);
    throw new Error(i`There was a failure during the installation of the python environement for the .espressif tools.`);
  }
}

export async function activateEspIdf(session: Session, targetLocation: Uri) {
  const pythonPath = await session.activation.getAlias('python');
  if (!pythonPath) {
    throw new Error(i`Python is not installed`);
  }

  const activateIdf = await execute(pythonPath, [
    targetLocation.join('tools', 'idf_tools.py').fsPath,
    'export',
    '--format',
    'key-value'
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
                session.activation.addPath(splitLine[0].trim(), session.fileSystem.file(path));
              }
            }
          }
        }
      });
    }
  });

  session.channels.verbose(JSON.stringify(activateIdf, null, 2));

  if (activateIdf.code) {
    session.channels.error(activateIdf.stderr);
    throw new Error(`Failed to activate esp-idf - ${activateIdf.stderr}`);
  }
}