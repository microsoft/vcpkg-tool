// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { bold, cyan, gray, green, greenBright, grey, underline, whiteBright, yellow, yellowBright } from 'chalk';
import { Uri } from '../util/uri';

export function projectFile(uri: Uri): string {
  return cyan(uri.fsPath);
}

export function prettyRegistryName(registryName: string) {
  return `${whiteBright(registryName)}`;
}

export function artifactIdentity(registryName: string, identity: string, shortName: string) : string {
  return `${whiteBright(registryName)}:${yellow.dim(identity.substr(0, identity.length - shortName.length))}${yellowBright(shortName)}`;
}

export function addVersionToArtifactIdentity(identity: string, version: string) {
  return version && version !== '*' ? `${identity}-${gray(version)}` : identity;
}

export function heading(text: string, level = 1) {
  switch (level) {
    case 1:
      return `${underline.bold(text)}`;
    case 2:
      return `${greenBright(text)}`;
    case 3:
      return `${green(text)}`;
  }
  return `${bold(text)}`;
}

export function optional(text: string) {
  return gray(text);
}
export function cmdSwitch(text: string) {
  return optional(`--${text}`);
}

export function command(text: string) {
  return whiteBright.bold(text);
}

export function hint(text: string) {
  return green.dim(text);
}

export function count(num: number) {
  return grey(`${num}`);
}

export function position(text: string) {
  return grey(`${text}`);
}
