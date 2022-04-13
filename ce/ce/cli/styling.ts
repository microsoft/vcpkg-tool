// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { blue, cyan, gray, green, red, white, yellow } from 'chalk';
import * as renderer from 'marked-terminal';
import { argv } from 'process';
import { Session } from '../session';
import { CommandLine } from './command-line';

// eslint-disable-next-line @typescript-eslint/no-var-requires
const marked = require('marked');

function formatTime(t: number) {
  return (
    t < 3600000 ? [Math.floor(t / 60000) % 60, Math.floor(t / 1000) % 60, t % 1000] :
      t < 86400000 ? [Math.floor(t / 3600000) % 24, Math.floor(t / 60000) % 60, Math.floor(t / 1000) % 60, t % 1000] :
        [Math.floor(t / 86400000), Math.floor(t / 3600000) % 24, Math.floor(t / 60000) % 60, Math.floor(t / 1000) % 60, t % 1000]).map(each => each.toString().padStart(2, '0')).join(':').replace(/(.*):(\d)/, '$1.$2');
}

// setup markdown renderer
marked.setOptions({
  renderer: new renderer({
    tab: 2,
    emoji: true,
    showSectionPrefix: false,
    firstHeading: green.underline.bold,
    heading: green.underline,
    codespan: white.bold,
    link: blue.bold,
    href: blue.bold.underline,
    code: gray,
    tableOptions: {
      chars: {
        'top': '', 'top-mid': '', 'top-left': '', 'top-right': ''
        , 'bottom': '', 'bottom-mid': '', 'bottom-left': '', 'bottom-right': ''
        , 'left': '', 'left-mid': '', 'mid': '', 'mid-mid': ''
        , 'right': '', 'right-mid': '', 'middle': ''
      }
    }
  }),
  gfm: true,
});

export function indent(text: string): string
export function indent(text: Array<string>): Array<string>
export function indent(text: string | Array<string>): string | Array<string> {
  if (Array.isArray(text)) {
    return text.map(each => indent(each));
  }
  return `  ${text}`;
}

function md(text = '', session?: Session): string {
  if (text) {
    text = marked.marked(`${text}`.replace(/\\\./g, '\\\\.')); // work around md messing up paths with .\ in them.

    // rewrite file:// urls to be locl filesystem urls.
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
  log = (text) => stdout((md(text, session).trim()));
  error = (text) => stdout(`${red.bold('ERROR: ')}${md(text, session).trim()}`);
  warning = (text) => stdout(`${yellow.bold('WARNING: ')}${md(text, session).trim()}`);
  debug = (text) => { if (commandline.debug) { stdout(`${cyan.bold('DEBUG: ')}${md(text, session).trim()}`); } };

  session.channels.on('message', (text: string, context: any, msec: number) => {
    log(text);
  });

  session.channels.on('error', (text: string, context: any, msec: number) => {
    error(text);
  });

  session.channels.on('debug', (text: string, context: any, msec: number) => {
    debug(`${cyan.bold(`[${formatTime(msec)}]`)} ${md(text, session)}`);
  });

  session.channels.on('warning', (text: string, context: any, msec: number) => {
    warning(text);
  });
}
