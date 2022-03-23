// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { InstallEvents } from '../interfaces/events';
import { Session } from '../session';
import { Credentials } from '../util/credentials';
import { execute } from '../util/exec-cmd';
import { isFilePath, Uri } from '../util/uri';

export interface CloneOptions {
  force?: boolean;
  credentials?: Credentials;
}

/** @internal */
export class Git {
  #session: Session;
  #toolPath: string;
  #targetFolder: string;
  #environment: NodeJS.ProcessEnv;

  constructor(session: Session, toolPath: string, environment: NodeJS.ProcessEnv, targetFolder: Uri) {
    this.#session = session;
    this.#toolPath = toolPath;
    this.#targetFolder = targetFolder.fsPath;
    this.#environment = environment;
  }

  /**
   * Method that clones a git repo into a desired location and with various options.
   * @param repo The Uri of the remote repository that is desired to be cloned.
   * @param events The events that may need to be updated in order to track progress.
   * @param options The options that will modify how the clone will be called.
   * @returns Boolean representing whether the execution was completed without error, this is not necessarily
   *  a gaurantee that the clone did what we expected.
   */
  async clone(repo: Uri, events: Partial<InstallEvents>, options: { recursive?: boolean, depth?: number } = {}) {
    const remote = await isFilePath(repo) ? repo.fsPath : repo.toString();

    const result = await execute(this.#toolPath, [
      'clone',
      remote,
      this.#targetFolder,
      options.recursive ? '--recursive' : '',
      options.depth ? `--depth=${options.depth}` : '',
      '--progress'
    ], {
      env: this.#environment,
      onStdErrData: (chunk) => {
        // generate progress events
        // this.#session.channels.debug(chunk.toString());
        const regex = /\s([0-9]*?)%/;
        chunk.toString().split(/^/gim).map((x: string) => x.trim()).filter((each: any) => each).forEach((line: string) => {
          const match_array = line.match(regex);
          if (match_array !== null) {
            events.heartbeat?.(line.trim());
          }
        });
      }
    });

    if (result.code) {
      return false;
    }

    return true;
  }

  /**
   * Fetches a 'tag', this could theoretically be a commit, a tag, or a branch.
   * @param remoteName Remote name to fetch from. Typically will be 'origin'.
   * @param events Events that may be called in order to present progress.
   * @param options Options to modify how fetch is called.
   * @returns Boolean representing whether the execution was completed without error, this is not necessarily
   *  a gaurantee that the fetch did what we expected.
   */
  async fetch(remoteName: string, events: Partial<InstallEvents>, options: { commit?: string, recursive?: boolean, depth?: number } = {}) {
    const result = await execute(this.#toolPath, [
      '-C',
      this.#targetFolder,
      'fetch',
      remoteName,
      options.commit ? options.commit : '',
      options.recursive ? '--recurse-submodules' : '',
      options.depth ? `--depth=${options.depth}` : ''
    ], {
      env: this.#environment
    });

    if (result.code) {
      return false;
    }

    return true;
  }

  /**
   * Checks out a specific commit. If no commit is given, the default behavior of a checkout will be
   * used. (Checking out the current branch)
   * @param events Events to possibly track progress.
   * @param options Passing along a commit or branch to checkout, optionally.
   * @returns Boolean representing whether the execution was completed without error, this is not necessarily
   *  a gaurantee that the checkout did what we expected.
   */
  async checkout(events: Partial<InstallEvents>, options: { commit?: string } = {}) {
    const result = await execute(this.#toolPath, [
      '-C',
      this.#targetFolder,
      'checkout',
      options.commit ? options.commit : ''
    ], {
      env: this.#environment
    });

    if (result.code) {
      return false;
    }

    return true;
  }

  /**
   * Performs a reset on the git repo.
   * @param events Events to possibly track progress.
   * @param options Options to control how the reset is called.
   * @returns Boolean representing whether the execution was completed without error, this is not necessarily
   *  a gaurantee that the reset did what we expected.
   */
  async reset(events: Partial<InstallEvents>, options: { commit?: string, recurse?: boolean, hard?: boolean } = {}) {
    const result = await execute(this.#toolPath, [
      '-C',
      this.#targetFolder,
      'reset',
      options.commit ? options.commit : '',
      options.recurse ? '--recurse-submodules' : '',
      options.hard ? '--hard' : ''
    ], {
      env: this.#environment
    });

    if (result.code) {
      return false;
    }

    return true;
  }
}
