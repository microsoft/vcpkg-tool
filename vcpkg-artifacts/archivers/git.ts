// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { UnpackEvents } from '../interfaces/events';
import { execute } from '../util/exec-cmd';
import { isFilePath, Uri } from '../util/uri';

export interface CloneOptions {
  force?: boolean;
}

/** @internal */
export class Git {
  #toolPath: string;
  #targetFolder: Uri;

  constructor(toolPath: string, targetFolder: Uri) {
    this.#toolPath = toolPath;
    this.#targetFolder = targetFolder;
  }

  /**
   * Method that clones a git repo into a desired location and with various options.
   * @param repo The Uri of the remote repository that is desired to be cloned.
   * @param events The events that may need to be updated in order to track progress.
   * @param options The options that will modify how the clone will be called.
   * @returns Boolean representing whether the execution was completed without error, this is not necessarily
   *  a guarantee that the clone did what we expected.
   */
  async clone(repo: Uri, events: Partial<UnpackEvents>, options: { recursive?: boolean, depth?: number } = {}) {
    const remote = await isFilePath(repo) ? repo.fsPath : repo.toString();

    const result = await execute(this.#toolPath, [
      'clone',
      remote,
      this.#targetFolder.fsPath,
      options.recursive ? '--recursive' : '',
      options.depth ? `--depth=${options.depth}` : '',
      '--progress'
    ], {
      onStdErrData: chunkToHeartbeat(events),
      onStdOutData: chunkToHeartbeat(events)
    });

    return result.code === 0 ? true : false;
  }

  /**
   * Fetches a 'tag', this could theoretically be a commit, a tag, or a branch.
   * @param remoteName Remote name to fetch from. Typically will be 'origin'.
   * @param events Events that may be called in order to present progress.
   * @param options Options to modify how fetch is called.
   * @returns Boolean representing whether the execution was completed without error, this is not necessarily
   *  a guarantee that the fetch did what we expected.
   */
  async fetch(remoteName: string, _events: Partial<UnpackEvents>, options: { commit?: string, depth?: number } = {}) {
    const result = await execute(this.#toolPath, [
      '-C',
      this.#targetFolder.fsPath,
      'fetch',
      remoteName,
      options.commit ? options.commit : '',
      options.depth ? `--depth=${options.depth}` : ''
    ], {
      cwd: this.#targetFolder.fsPath
    });

    return result.code === 0 ? true : false;
  }

  /**
   * Checks out a specific commit. If no commit is given, the default behavior of a checkout will be
   * used. (Checking out the current branch)
   * @param events Events to possibly track progress.
   * @param options Passing along a commit or branch to checkout, optionally.
   * @returns Boolean representing whether the execution was completed without error, this is not necessarily
   *  a guarantee that the checkout did what we expected.
   */
  async checkout(events: Partial<UnpackEvents>, options: { commit?: string } = {}) {
    const result = await execute(this.#toolPath, [
      '-C',
      this.#targetFolder.fsPath,
      'checkout',
      options.commit ? options.commit : ''
    ], {
      cwd: this.#targetFolder.fsPath,
      onStdErrData: chunkToHeartbeat(events),
      onStdOutData: chunkToHeartbeat(events)
    });
    return result.code === 0 ? true : false;
  }


  /**
   * Performs a reset on the git repo.
   * @param events Events to possibly track progress.
   * @param options Options to control how the reset is called.
   * @returns Boolean representing whether the execution was completed without error, this is not necessarily
   *  a guarantee that the reset did what we expected.
   */
  async reset(events: Partial<UnpackEvents>, options: { commit?: string, recurse?: boolean, hard?: boolean } = {}) {
    const result = await execute(this.#toolPath, [
      '-C',
      this.#targetFolder.fsPath,
      'reset',
      options.commit ? options.commit : '',
      options.recurse ? '--recurse-submodules' : '',
      options.hard ? '--hard' : ''
    ], {
      cwd: this.#targetFolder.fsPath,
      onStdErrData: chunkToHeartbeat(events),
      onStdOutData: chunkToHeartbeat(events)
    });
    return result.code === 0 ? true : false;
  }


  /**
   * Initializes a folder on disk to be a git repository
   * @returns true if the initialization was successful, false otherwise.
   */
  async init() {
    if (! await this.#targetFolder.exists()) {
      await this.#targetFolder.createDirectory();
    }

    if (! await this.#targetFolder.isDirectory()) {
      throw new Error(`${this.#targetFolder.fsPath} is not a directory.`);
    }

    const result = await execute(this.#toolPath, ['init'], {
      cwd: this.#targetFolder.fsPath
    });

    return result.code === 0 ? true : false;
  }

  /**
   * Adds a remote location to the git repo.
   * @param name the name of the remote to add.
   * @param location the location of the remote to add.
   * @returns true if the addition was successful, false otherwise.
   */
  async addRemote(name: string, location: Uri) {
    const result = await execute(this.#toolPath, [
      '-C',
      this.#targetFolder.fsPath,
      'remote',
      'add',
      name,
      location.toString()
    ], {
      cwd: this.#targetFolder.fsPath
    });

    return result.code === 0;
  }

  /**
   * updates submodules in a git repository
   * @param events Events to possibly track progress.
   * @param options Options to control how the submodule update is called.
   * @returns true if the update was successful, false otherwise.
   */
  async updateSubmodules(events: Partial<UnpackEvents>, options: { init?: boolean, recursive?: boolean, depth?: number } = {}) {
    const result = await execute(this.#toolPath, [
      '-C',
      this.#targetFolder.fsPath,
      'submodule',
      'update',
      '--progress',
      options.init ? '--init' : '',
      options.depth ? `--depth=${options.depth}` : '',
      options.recursive ? '--recursive' : '',
    ], {
      cwd: this.#targetFolder.fsPath,
      onStdErrData: chunkToHeartbeat(events),
      onStdOutData: chunkToHeartbeat(events)
    });

    return result.code === 0;
  }

  /**
   * sets a git configuration value in the repo.
   * @param configFile the relative path to the config file inside the repo on disk
   * @param key the key to set in the config file
   * @param value the value to set in the config file
   * @returns true if the config file was updated, false otherwise
   */
  async config(configFile: string, key: string, value: string) {
    const result = await execute(this.#toolPath, [
      'config',
      '-f',
      this.#targetFolder.join(configFile).fsPath,
      key,
      value
    ], {
      cwd: this.#targetFolder.fsPath
    });
    return result.code === 0;
  }
}
function chunkToHeartbeat(events: Partial<UnpackEvents>): (chunk: any) => void {
  return (chunk: any) => {
    const regex = /\s([0-9]*?)%/;
    chunk.toString().split(/^/gim).map((x: string) => x.trim()).filter((each: any) => each).forEach((line: string) => {
      const match_array = line.match(regex);
      if (match_array !== null) {
        events.unpackArchiveHeartbeat?.(line.trim());
      }
    });
  };
}
