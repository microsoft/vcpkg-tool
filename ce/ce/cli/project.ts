// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ArtifactMap, ProjectManifest } from '../artifacts/artifact';
import { i } from '../i18n';
import { trackActivation } from '../insights';
import { session } from '../main';
import { Uri } from '../util/uri';
import { installArtifacts, showArtifacts } from './artifacts';
import { blank } from './constants';
import { projectFile } from './format';
import { error, log } from './styling';

class ActivationOptions {
  force?: boolean;
  allLanguages?: boolean;
  language?: string;
  msbuildProps?: Uri;
  json?: Uri;
}

export async function openProject(location: Uri): Promise<ProjectManifest> {
  // load the project
  return new ProjectManifest(session, await session.openManifest(location));
}

export async function activate(artifacts: ArtifactMap, createUndoFile: boolean, options?: ActivationOptions) {
  // install the items in the project
  const [success, artifactStatus] = await installArtifacts(session, artifacts.artifacts, options);

  if (success) {
    const backupFile = createUndoFile ? session.tmpFolder.join(`previous-environment-${Date.now().toFixed()}.json`) : undefined;
    await session.activation.activate(artifacts.artifacts, session.environment, session.postscriptFile, backupFile, options?.msbuildProps, options?.json);
  }

  return success;
}

export async function activateProject(project: ProjectManifest, options?: ActivationOptions) {
  // track what got installed
  const artifacts = await project.resolveDependencies();

  // print the status of what is going to be activated.
  if (!await showArtifacts(artifacts.artifacts, options)) {
    error(i`Unable to activate project`);
    return false;
  }

  if (await activate(artifacts, true, options)) {
    trackActivation();
    log(blank);
    log(i`Project ${projectFile(project.metadata.context.folder)} activated`);
    return true;
  }

  log(blank);
  log(i`Failed to activate project ${projectFile(project.metadata.context.folder)}`);

  return false;
}
