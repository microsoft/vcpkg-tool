// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ChildProcess, spawn, SpawnOptions } from 'child_process';
import { lstat } from 'fs/promises';

export interface ExecOptions extends SpawnOptions {
  onCreate?(cp: ChildProcess): void;
  onStdOutData?(chunk: any): void;
  onStdErrData?(chunk: any): void;
}

export interface ExecResult {
  stdout: string;
  stderr: string;
  env: NodeJS.ProcessEnv | undefined;

  /**
   * Union of stdout and stderr.
   */
  log: string;
  error: Error | null;
  code: number | null;
  command: string,
  args: Array<string>,
}

export function cmdlineToArray(text: string, result: Array<string> = [], matcher = /[^\s"]+|"([^"]*)"/gi): Array<string> {
  text = text.replace(/\\"/g, '\ufffe');
  const match = matcher.exec(text);
  if (match) {
    result.push(match[1] ? match[1].replace(/\ufffe/g, '\\"') : match[0].replace(/\ufffe/g, '\\"'));
    return cmdlineToArray(text, result, matcher);
  }

  return result;
}

export async function execute(command: string, cmdlineargs: Array<string>, options: ExecOptions = {}): Promise<ExecResult> {
  try {
    command = command.replace(/"/g, '');
    const k = await lstat(command);
    if (k.isDirectory()) {
      throw new Error(`Unable to call ${command} ${cmdlineargs.join(' ')} -- ${command} is a directory`);
    }
  } catch {
    throw new Error(`Unable to call ${command} ${cmdlineargs.join(' ')} - -- ${command} is not a file `);

  }

  return new Promise((resolve, reject) => {
    const cp = spawn(command, cmdlineargs.filter(each => each), { ...options, stdio: 'pipe' });
    if (options.onCreate) {
      options.onCreate(cp);
    }

    if (options.onStdOutData) { cp.stdout.on('data', options.onStdOutData); }
    if (options.onStdErrData) { cp.stderr.on('data', options.onStdErrData); }

    let err = '';
    let out = '';
    let all = '';
    cp.stderr.on('data', (chunk) => {
      err += chunk;
      all += chunk;
    });
    cp.stdout.on('data', (chunk) => {
      out += chunk;
      all += chunk;
    });

    cp.on('error', (err) => {
      reject(err);
    });

    cp.on('close', (code) => {
      return resolve({
        env: options.env,
        stdout: out,
        stderr: err,
        log: all,
        error: code ? new Error('Process Failed.') : null,
        code,
        command: command,
        args: cmdlineargs,
      });
    }
    );
  });
}
