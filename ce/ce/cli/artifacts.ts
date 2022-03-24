// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { MultiBar, SingleBar } from 'cli-progress';
import { Activation } from '../artifacts/activation';
import { Artifact, ArtifactMap } from '../artifacts/artifact';
import { i } from '../i18n';
import { trackAcquire } from '../insights';
import { Registries } from '../registries/registries';
import { Session } from '../session';
import { artifactIdentity, artifactReference } from './format';
import { Table } from './markdown-table';
import { debug, error, log } from './styling';

export async function showArtifacts(artifacts: Iterable<Artifact>, options?: { force?: boolean }) {
  let failing = false;
  const table = new Table(i`Artifact`, i`Version`, i`Status`, i`Dependency`, i`Summary`);
  for (const artifact of artifacts) {
    const name = artifactIdentity(artifact.registryId, artifact.id, artifact.shortName);
    if (!artifact.metadata.isValid) {
      failing = true;
      for (const err of artifact.metadata.validationErrors) {
        error(err);
      }
    }
    table.push(name, artifact.version, options?.force || await artifact.isInstalled ? 'installed' : 'will install', artifact.isPrimary ? ' ' : '*', artifact.metadata.info.summary || '');
  }
  log(table.toString());

  return !failing;
}

export type Selections = Map<string, string>;

export async function selectArtifacts(selections: Selections, registries: Registries): Promise<false | ArtifactMap> {
  const artifacts = new ArtifactMap();

  for (const [identity, version] of selections) {
    const [registry, id, artifact] = await registries.getArtifact(identity, version) || [];

    if (!artifact) {
      error(`Unable to resolve artifact: ${artifactReference('', identity, version)}`);
      return false;
    }

    artifacts.set(artifact.uniqueId, [artifact, identity, version]);
    artifact.isPrimary = true;
    await artifact.resolveDependencies(artifacts);
  }
  return artifacts;
}

export async function installArtifacts(session: Session, artifacts: Iterable<Artifact>, options?: { force?: boolean, allLanguages?: boolean, language?: string }): Promise<[boolean, Map<Artifact, boolean>, Activation]> {
  // resolve the full set of artifacts to install.
  const installed = new Map<Artifact, boolean>();
  const activation = new Activation(session);

  const bar = new MultiBar({
    clearOnComplete: true, hideCursor: true, format: '{name} {bar}\u25A0 {percentage}% {action} {current}',
    barCompleteChar: '\u25A0',
    barIncompleteChar: ' ',
    etaBuffer: 40
  });
  let dl: SingleBar | undefined;
  let p: SingleBar | undefined;
  let spinnerValue = 0;

  for (const artifact of artifacts) {
    const id = artifact.id;
    const registryName = artifact.registryId;

    try {
      const actuallyInstalled = await artifact.install(activation, {
        verifying: (name, percent) => {
          if (percent >= 100) {
            p?.update(percent);
            p = undefined;
            return;
          }
          if (percent) {
            if (!p) {
              p = bar.create(100, 0, { action: i`verifying`, name: artifactIdentity(registryName, id), current: name });
            }
            p?.update(percent);
          }
        },
        download: (name, percent) => {
          if (percent >= 100) {
            if (dl) {
              dl.update(percent);
            }
            dl = undefined;
            return;
          }
          if (percent) {
            if (!dl) {
              dl = bar.create(100, 0, { action: i`downloading`, name: artifactIdentity(registryName, id), current: name });
            }
            dl.update(percent);
          }
        },
        fileProgress: (entry) => {
          p?.update({ action: i`unpacking`, name: artifactIdentity(registryName, id), current: entry.extractPath });
        },
        progress: (percent: number) => {
          if (percent >= 100) {
            if (p) {
              p.update(percent, { action: i`unpacked`, name: artifactIdentity(registryName, id), current: '' });
            }
            p = undefined;
            return;
          }
          if (percent) {
            if (!p) {
              p = bar.create(100, 0, { action: i`unpacking`, name: artifactIdentity(registryName, id), current: '' });
            }
            p.update(percent);
          }
        },
        heartbeat: (text: string) => {
          if (!p) {
            p = bar.create(3, 0, { action: i`working`, name: artifactIdentity(registryName, id), current: '' });
          }
          p?.update((spinnerValue++) % 4, { action: i`working`, name: artifactIdentity(registryName, id), current: text });
        }
      }, options || {});
      // remember what was actually installed
      installed.set(artifact, actuallyInstalled);
      if (actuallyInstalled) {
        trackAcquire(artifact.id, artifact.version);
      }
    } catch (e: any) {
      bar.stop();
      debug(e);
      debug(e.stack);
      error(i`Error installing ${artifactIdentity(registryName, id)} - ${e} `);
      return [false, installed, activation];
    }

    bar.stop();
  }
  return [true, installed, activation];
}

export async function activateArtifacts(session: Session, artifacts: Iterable<Artifact>) {
  const activation = new Activation(session);
  for (const artifact of artifacts) {
    if (await artifact.isInstalled) {
      await artifact.loadActivationSettings(activation);
    }
  }
  return activation;
}
