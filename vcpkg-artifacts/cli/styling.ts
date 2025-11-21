// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import chalk from 'chalk';
import { argv } from 'process';
import { i } from '../i18n';
import { Session } from '../session';

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

export const log: (message?: any) => void = console.log;
export const error: (message?: any) => void = (text) => {
  const errorLocalized = i`error:`;
  return console.log(`${chalk.red.bold(errorLocalized)} ${text}`);
};
export const warning: (message?: any) => void = (text) => {
  const warningLocalized = i`warning:`;
  return console.log(`${chalk.yellow.bold(warningLocalized)} ${text}`);
};
export const debug: (message?: any) => void = (text) => {
  if (argv.any(arg => arg === '--debug')) {
    console.log(`${chalk.cyan.bold('debug: ')}${text}`);
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

export function initStyling(session: Session) {

  session.channels.on('message', (text: string, _msec: number) => {
    log(text);
  });

  session.channels.on('error', (text: string, _msec: number) => {
    error(text);
  });

  session.channels.on('debug', (text: string, msec: number) => {
    debug(`${chalk.cyan.bold(`[${formatTime(msec)}]`)} ${text}`);
  });

  session.channels.on('warning', (text: string, _msec: number) => {
    warning(text);
  });
}
