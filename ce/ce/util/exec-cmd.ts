// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ChildProcess, ProcessEnvOptions, spawn, SpawnOptions } from 'child_process';

export interface ExecOptions extends SpawnOptions {
  onCreate?(cp: ChildProcess): void;
  onStdOutData?(chunk: any): void;
  onStdErrData?(chunk: any): void;
}

export interface ExecResult {
  stdout: string;
  stderr: string;

  /**
   * Union of stdout and stderr.
   */
  log: string;
  error: Error | null;
  code: number | null;
}


export function cmdlineToArray(text: string, result: Array<string> = [], matcher = /[^\s"]+|"([^"]*)"/gi, count = 0): Array<string> {
  text = text.replace(/\\"/g, '\ufffe');
  const match = matcher.exec(text);
  return match
    ? cmdlineToArray(
      text,
      result,
      matcher,
      result.push(match[1] ? match[1].replace(/\ufffe/g, '\\"') : match[0].replace(/\ufffe/g, '\\"')),
    )
    : result;
}

export function execute(command: string, cmdlineargs: Array<string>, options: ExecOptions = {}): Promise<ExecResult> {
  return new Promise((resolve, reject) => {
    const cp = spawn(command, cmdlineargs.filter(each => each), { ...options, stdio: 'pipe' });
    if (options.onCreate) {
      options.onCreate(cp);
    }

    options.onStdOutData ? cp.stdout.on('data', options.onStdOutData) : cp;
    options.onStdErrData ? cp.stderr.on('data', options.onStdErrData) : cp;

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

    cp.on('close', (code, signal) =>
      resolve({
        stdout: out,
        stderr: err,
        log: all,
        error: code ? new Error('Process Failed.') : null,
        code,
      }),
    );
  });
}

/**
 * Method that wraps spawn with for ease of calling. It is wrapped with a promise that must be awaited.
 * This version calls spawn with a shell and allows for chaining of commands. In this version, commands and
 * arguments must both be given in the command string.
 * @param commands String of commands with parameters, possibly chained together with '&&' or '||'.
 * @param options Options providing callbacks for various scenarios.
 * @param environmentOptions Environment options for passing environment variables into the spawn.
 * @returns
 */
export const execute_shell = (
  command: string,
  options: ExecOptions = {},
  environmentOptions?: ProcessEnvOptions
): Promise<ExecResult> => {
  return new Promise((resolve, reject) => {
    const cp = spawn(command, environmentOptions ? { ...options, ...environmentOptions, stdio: 'pipe', shell: true } : { ...options, stdio: 'pipe', shell: true });
    if (options.onCreate) {
      options.onCreate(cp);
    }

    options.onStdOutData ? cp.stdout.on('data', options.onStdOutData) : cp;
    options.onStdErrData ? cp.stderr.on('data', options.onStdErrData) : cp;

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
    cp.on('close', (code, signal) =>
      resolve({
        stdout: out,
        stderr: err,
        log: all,
        error: code ? new Error('Process Failed.') : null,
        code,
      }),
    );
  });
};