// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import chalk from 'chalk';
import { Uri } from '../util/uri';

export function projectFile(uri: Uri): string {
  return chalk.cyan(uri.fsPath);
}

export function prettyRegistryName(registryName: string) {
  return `${chalk.whiteBright(registryName)}`;
}

export function artifactIdentity(registryName: string, identity: string, shortName: string) : string {
  return `${chalk.whiteBright(registryName)}:${chalk.yellow.dim(identity.substr(0, identity.length - shortName.length))}${chalk.yellowBright(shortName)}`;
}

export function addVersionToArtifactIdentity(identity: string, version: string) {
  return version && version !== '*' ? `${identity}-${chalk.gray(version)}` : identity;
}

export function heading(text: string, level = 1) {
  switch (level) {
    case 1:
      return `${chalk.underline.bold(text)}`;
    case 2:
      return `${chalk.greenBright(text)}`;
    case 3:
      return `${chalk.green(text)}`;
  }
  return `${chalk.bold(text)}`;
}

export function optional(text: string) {
  return chalk.gray(text);
}
export function cmdSwitch(text: string) {
  return optional(`--${text}`);
}

export function command(text: string) {
  return chalk.whiteBright.bold(text);
}

export function hint(text: string) {
  return chalk.green.dim(text);
}

export function count(num: number) {
  return chalk.grey(`${num}`);
}

export function position(text: string) {
  return chalk.grey(`${text}`);
}
