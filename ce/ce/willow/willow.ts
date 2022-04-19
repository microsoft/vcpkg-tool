// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../i18n';

export interface VsPayload {
  readonly fileName: string;
  readonly sha256: string;
  readonly size: number;
  readonly url: string;
}

export interface ChManDef {
  readonly payloads: Array<VsPayload>;
  readonly type: string;
  readonly version: string;
}

export interface FlatChManPayload extends VsPayload {
  readonly version: string;
}

export interface FlatVsManPayload extends VsPayload {
  readonly id: string;
  readonly version: string;
  readonly language: string | undefined;
  readonly installSize: number;
  readonly localPath: string;
}

function throwUnexpectedManifest(): never {
  throw new Error(i`Unexpected format for Visual Studio manifest in channel.`);
}

function isPayload(candidatePayload: any) {
  return typeof candidatePayload.fileName === 'string'
    && typeof candidatePayload.sha256 === 'string'
    && candidatePayload.sha256.length === 64
    && typeof candidatePayload.size === 'number'
    && typeof candidatePayload.url === 'string';
}

function isMinimalPackage(candidatePackage: any) {
  return typeof candidatePackage.id === 'string'
    && typeof candidatePackage.version === 'string'
    && candidatePackage.type === 'Vsix'
    && Array.isArray(candidatePackage.payloads)
    && candidatePackage.payloads.length === 1
    && isPayload(candidatePackage.payloads[0]);
}

function packageHasInstallSize(candidatePackage: any) {
  return typeof (candidatePackage.installSizes) === 'object'
    && typeof (candidatePackage.installSizes.targetDrive) === 'number';
}

function packageGetPointerLikeSingleDependencyId(candidatePackage: any): string | undefined {
  if (packageHasInstallSize(candidatePackage)
    || typeof candidatePackage.dependencies !== 'object') {
    return undefined;
  }

  let theDependency = undefined;
  for (const key in candidatePackage.dependencies) {
    if (theDependency) {
      return undefined;
    }

    if (key !== '') {
      theDependency = key;
    }
  }

  return theDependency;
}

export function parseVsManFromChannel(channelManifestContent: string): FlatChManPayload {
  const chMan = JSON.parse(channelManifestContent);
  const channelItemsRaw = chMan?.channelItems;
  if (chMan.manifestVersion !== '1.1' || !Array.isArray(channelItemsRaw)) {
    throw new Error(i`Unexpected Visual Studio channel manifest version.`);
  }

  let vsManItem: ChManDef | undefined = undefined;
  for (const channelItem of channelItemsRaw) {
    if (channelItem?.id === 'Microsoft.VisualStudio.Manifests.VisualStudio') {
      if (vsManItem === undefined) {
        vsManItem = <ChManDef>channelItem;
      } else {
        throwUnexpectedManifest();
      }
    }
  }

  if (!vsManItem) {
    throwUnexpectedManifest();
  }

  if (!Array.isArray(vsManItem.payloads)
    || vsManItem.type !== 'Manifest'
    || typeof vsManItem.version !== 'string'
    || vsManItem.payloads.length !== 1
    || !isPayload(vsManItem.payloads[0])) {
    throwUnexpectedManifest();
  }

  return { ...vsManItem.payloads[0], version: vsManItem.version };
}

function flattenVsManPackage(rawJson: any): FlatVsManPayload | undefined {
  if (!packageHasInstallSize(rawJson)) {
    return undefined;
  }

  let language: string | undefined;
  if (typeof (rawJson.language) === 'string') {
    language = rawJson.language;
  }

  const thePayload = rawJson.payloads[0];
  const theId = <string>rawJson.id;
  const theVersion = <string>rawJson.version;
  let chip: string | undefined;
  if (typeof (rawJson.chip) === 'string') {
    chip = rawJson.chip;
  }

  let localPath = `vsix:///${theId},version=${theVersion}`;
  if (chip) {
    localPath += `,chip=${chip}`;
  }

  if (language) {
    localPath += `,language=${language}`;
  }

  localPath += '/payload.vsix';

  return {
    fileName: <string>thePayload.fileName,
    sha256: <string>thePayload.sha256,
    size: <number>thePayload.size,
    url: <string>thePayload.url,
    id: theId,
    version: theVersion,
    language: language,
    installSize: <number>rawJson.installSizes.targetDrive,
    localPath: localPath
  };
}

function maybeHydratePointerLikePackages(lookup: Map<string, Array<any>>, originalId: string) {
  const originalArray = lookup.get(originalId);
  if (!originalArray || originalArray.length === 0) {
    return;
  }

  const targetId = packageGetPointerLikeSingleDependencyId(originalArray[0]);
  if (targetId && targetId !== originalId) {
    maybeHydratePointerLikePackages(lookup, targetId);
    const targetArray = lookup.get(targetId);
    if (targetArray && targetArray.length == originalArray.length) {
      lookup.set(originalId, targetArray);
    }
  }
}

function escapeRegex(str: string) {
  return str.replace(/[-/\\^$*+?.()|[\]{}]/g, '\\$&');
}

class NumbersEntry {
  public readonly numbers: Array<number>; // for example, [10, 29]
  constructor(public readonly rawStr: string) // for example, 10.0029
  {
    this.numbers = Array.from(rawStr.split('.').select(Number.parseInt));
  }
}

function cmpNumbersEntry(a: NumbersEntry, b: NumbersEntry) {
  for (let idx = 0; ; ++idx) {
    if (a.numbers.length <= idx) {
      if (b.numbers.length <= idx) {
        return 0;
      }

      return -1;
    } else if (b.numbers.length <= idx) {
      return 1;
    }

    const l = a.numbers[idx];
    const r = b.numbers[idx];
    if (l < r) {
      return -1;
    }

    if (r < l) {
      return 1;
    }
  }
}

function maxElement<T>(subject: Array<T>, cmp: (arg0: T, arg1: T) => number): T | undefined {
  if (subject.length === 0) {
    return undefined;
  }

  let highest = subject[0];
  for (let idx = 1; idx < subject.length; ++idx) {
    if (cmp(subject[idx], highest) > 0) {
      highest = subject[idx];
    }
  }

  return highest;
}

export function resolveVsManId(ids: Iterable<string>, candidateId: string): string {
  const needle = '$(SxSVersion)';
  const loc = candidateId.indexOf(needle);
  if (loc >= 0) {
    if (candidateId.indexOf(needle, loc + 1) >= 0) {
      throw new Error(i`${candidateId} can only contain ${needle} once`);
    }

    // find all the ids which match the expected form
    const matchingIds = new Array<NumbersEntry>();
    const prefix = candidateId.slice(0, loc);
    const suffix = candidateId.slice(loc + needle.length);
    const regStr = '^' + escapeRegex(prefix) + '((?:\\d+\\.)*\\d+)' + escapeRegex(suffix) + '$';
    const reg = new RegExp(regStr);
    for (const id of ids) {
      const match = reg.exec(id);
      if (match) {
        matchingIds.push(new NumbersEntry(match[1]));
      }
    }

    const highest = maxElement(matchingIds, cmpNumbersEntry);
    if (highest) {
      return prefix + highest.rawStr + suffix;
    }
  }

  return candidateId;
}

export class VsManDatabase {
  private readonly idLookup = new Map<string, Array<FlatVsManPayload>>();

  constructor(vsManContent: string) {
    const vsMan = JSON.parse(vsManContent);
    if (!Array.isArray(vsMan.packages)) {
      throwUnexpectedManifest();
    }

    const lookup = new Map<string, Array<any>>();
    for (const p of vsMan.packages) {
      if (!isMinimalPackage(p)) {
        continue;
      }

      const id = p.id;
      const existing = lookup.get(id);
      if (existing) {
        existing.push(p);
      } else {
        lookup.set(id, [p]);
      }
    }

    for (const id of Array.from(lookup.keys())) {
      maybeHydratePointerLikePackages(lookup, id);
    }

    for (const entry of lookup.entries()) {
      const newArr = new Array<FlatVsManPayload>();
      for (const p of entry[1]) {
        const flattened = flattenVsManPackage(p);
        if (flattened) {
          newArr.push(flattened);
        }
      }

      if (newArr.length != 0) {
        this.idLookup.set(entry[0], newArr);
      }
    }
  }

  get(id: string): Array<FlatVsManPayload> | undefined {
    return this.idLookup.get(resolveVsManId(this.idLookup.keys(), id));
  }

  get size() {
    return this.idLookup.size;
  }
}
