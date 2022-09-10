// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { cyan, green, red, yellow } from 'chalk';
import { argv } from 'process';
import { Artifact } from '../artifacts/artifact';
import { Session } from '../session';
import { CommandLine } from './command-line';
import { artifactIdentity } from './format';
import chalk = require('chalk');

function formatTime(t: number) {
  return (
    t < 3600000 ? [Math.floor(t / 60000) % 60, Math.floor(t / 1000) % 60, t % 1000] :
      t < 86400000 ? [Math.floor(t / 3600000) % 24, Math.floor(t / 60000) % 60, Math.floor(t / 1000) % 60, t % 1000] :
        [Math.floor(t / 86400000), Math.floor(t / 3600000) % 24, Math.floor(t / 60000) % 60, Math.floor(t / 1000) % 60, t % 1000]).map(each => each.toString().padStart(2, '0')).join(':').replace(/(.*):(\d)/, '$1.$2');
}

export function indent(text: string): string
export function indent(text: Array<string>): Array<string>
export function indent(text: string | Array<string>): string | Array<string> {
  if (Array.isArray(text)) {
    return text.map(each => indent(each));
  }
  return `  ${text}`;
}

function reformatText(text = '', session?: Session): string {
  if (text) {
    text = `${text}`.replace(/\\\./g, '\\\\.');

    // rewrite file:// urls to be local filesystem urls.
    return (!!text && !!session) ? text.replace(/(file:\/\/\S*)/g, (s, a) => yellow.dim(session.parseUri(a).fsPath)) : text;
  }
  return '';
}

const stdout = console['log'];

export let log: (message?: any, ...optionalParams: Array<any>) => void = stdout;
export let error: (message?: any, ...optionalParams: Array<any>) => void = console.error;
export let warning: (message?: any, ...optionalParams: Array<any>) => void = console.error;
export let debug: (message?: any, ...optionalParams: Array<any>) => void = (text) => {
  if (argv.any(arg => arg === '--debug')) {
    stdout(`${cyan.bold('debug: ')}${text}`);
  }
};

export function writeException(e: any) {
  if (e instanceof Error) {
    debug(e.message);
    debug(e.stack);
    return;
  }
  debug(e && e.toString ? e.toString() : e);
}

export function initStyling(commandline: CommandLine, session: Session) {
  if (commandline.switches['no-color']) {
    chalk.level = 0;
  }
  log = (text) => stdout((reformatText(text, session).trim()));
  error = (text) => stdout(`${red.bold('\nERROR: ')}${reformatText(text, session).trim()}`);
  warning = (text) => stdout(`${yellow.bold('\nWARNING: ')}${reformatText(text, session).trim()}`);
  debug = (text) => { if (commandline.debug) { stdout(`${cyan.bold('DEBUG: ')}${reformatText(text, session).trim()}`); } };

  session.channels.on('message', (text: string, context: any, msec: number) => {
    if (context && context instanceof Artifact) {
      log(`${green.bold('NOTE: ')}[${artifactIdentity(context.registryId, context.id)}] - ${text}`);
    } else {
      log(text);
    }
  });

  session.channels.on('error', (text: string, context: any, msec: number) => {
    if (context && context instanceof Artifact) {
      error(`[${artifactIdentity(context.registryId, context.id)}] - ${text}`);
    } else {
      error(text);
    }
  });

  session.channels.on('debug', (text: string, context: any, msec: number) => {
    if (context && context instanceof Artifact) {
      debug(`[${artifactIdentity(context.registryId, context.id)}] - ${text}`);
    } else {
      debug(`${cyan.bold(`[${formatTime(msec)}]`)} ${reformatText(text, session)}`);
    }
  });

  session.channels.on('verbose', (text: string, context: any, msec: number) => {
    if (commandline.verbose) {
      debug(` ${green(text)}`);
    }
  });

  session.channels.on('warning', (text: string, context: any, msec: number) => {
    if (context && context instanceof Artifact) {
      warning(`[${artifactIdentity(context.registryId, context.id)}] - ${text}`);
    } else {
      warning(text);
    }
  });
}
