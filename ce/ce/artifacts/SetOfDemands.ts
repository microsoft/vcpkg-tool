// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { MetadataFile } from '../amf/metadata-file';
import { Demands } from '../interfaces/metadata/demands';
import { VersionReference } from '../interfaces/metadata/version-reference';
import { parseQuery } from '../mediaquery/media-query';
import { Session } from '../session';
import { MultipleInstallsMatched } from '../util/exceptions';
import { Dictionary, linq } from '../util/linq';
import { Activation } from './activation';


export class SetOfDemands {
  _demands = new Map<string, Demands>();

  constructor(metadata: MetadataFile, session: Session) {
    this._demands.set('', metadata);

    for (const [query, demands] of metadata.conditionalDemands) {
      if (parseQuery(query).match(session.context)) {
        session.channels.debug(`Matching demand query: '${query}'`);
        this._demands.set(query, demands);
      }
    }
  }

  setActivation(activation: Activation) {
    for (const [, demandBlock] of this._demands.entries()) {
      demandBlock.setActivation(activation);
    }
  }

  /** Async Initializer */
  async init(session: Session) {
    for (const [query, demands] of this._demands) {
      await demands.init(session);
    }
  }

  get installer() {
    const install = linq.entries(this._demands).where(([query, demand]) => demand.install.length > 0).toArray();

    if (install.length > 1) {
      // bad. There should only ever be one install block.
      throw new MultipleInstallsMatched(install.map(each => each[0]));
    }

    return install[0]?.[1].install || [];
  }

  get errors() {
    return linq.values(this._demands).selectNonNullable(d => d.error).toArray();
  }
  get warnings() {
    return linq.values(this._demands).selectNonNullable(d => d.warning).toArray();
  }
  get messages() {
    return linq.values(this._demands).selectNonNullable(d => d.message).toArray();
  }
  get settings() {
    return linq.values(this._demands).selectNonNullable(d => d.settings).toArray();
  }
  get seeAlso() {
    return linq.values(this._demands).selectNonNullable(d => d.seeAlso).toArray();
  }

  get requires() {
    const d = this._demands;
    const rq1 = linq.values(d).selectNonNullable(d => d.requires).toArray();
    const result = new Dictionary<VersionReference>();
    for (const dict of rq1) {
      for (const [query, demands] of dict) {
        result[query] = demands;
      }
    }
    const rq = [...d.values()].map(each => each.requires).filter(each => each);

    for (const dict of rq) {
      for (const [query, demands] of dict) {
        result[query] = demands;
      }
    }
    return result;
  }
}
