// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { LazyPromise, ManualPromise } from './manual-promise';

/** a precrafted failed Promise */
const waiting = Promise.reject(0xDEFACED);
waiting.catch(() => { /** */ });

/**
 * Does a Promise.any(), and accept the one that first matches the predicate, or if all resolve, and none match, the first.
 *
 * @remarks WARNING - this requires Node 15+ or node 14.12+ with --harmony
 * @param from
 * @param predicate
 */
export async function anyWhere<T>(from: Iterable<Promise<T>>, predicate: (value: T) => boolean) {
  let unfulfilled = new Array<Promise<T>>();
  const failed = new Array<Promise<T>>();
  const completed = new Array<T>();

  // wait for something to succeed. if nothing suceeds, then this will throw.
  const first = await Promise.any(from);
  let success: T | undefined;

  // eslint-disable-next-line no-constant-condition
  while (true) {

    //
    for (const each of from) {
      // if we had a winner, return now.
      await Promise.any([each, waiting]).then(antecedent => {
        if (predicate(antecedent)) {
          success = antecedent;
          return antecedent;
        }
        completed.push(antecedent);
        return undefined;
      }).catch(r => {
        if (r === 0xDEFACED) {
          // it's not done yet.
          unfulfilled.push(each);
        } else {
          // oh, it returned and it was a failure.
          failed.push(each);
        }
        return undefined;
      });
    }
    // we found one that passes muster!
    if (success) {
      return success;
    }

    if (unfulfilled.length) {
      // something completed successfully, but nothing passed the predicate yet.
      // so hope remains eternal, lets rerun whats left with the unfulfilled.
      from = unfulfilled;
      unfulfilled = [];
      continue;
    }

    // they all finished
    // but nothing hit the happy path.
    break;
  }

  // if we get here, then we're
  // everything completed, but nothing passed the predicate
  // give them the first to suceed
  return first;
}


export class Queue {
  private total = 0;
  private active = 0;
  private queue = new Array<LazyPromise<any>>();
  private whenZero: ManualPromise<number> | undefined;
  private rejections = new Array<any>();

  constructor(private maxConcurency = 8) {
  }

  get count() {
    return this.total;
  }

  get done() {
    return this.zero();
  }

  /** Will block until the queue hits the zero mark */
  private async zero(): Promise<number> {
    if (this.active) {
      this.whenZero = this.whenZero || new ManualPromise<number>();
      await this.whenZero;
    }
    if (this.rejections.length > 0) {
      throw new AggregateError(this.rejections);
    }
    this.whenZero = undefined;
    return this.total;
  }

  private next() {
    (--this.active) || this.whenZero?.resolve(0);
    if (this.queue.length) {
      this.queue.pop()?.execute().catch(async (e) => { this.rejections.push(e); throw e; }).finally(() => this.next());
    }
  }

  /**
   * Queues up actions for throttling the number of concurrent async tasks running at a given time.
   *
   * If the process has reached max concurrency, the action is deferred until the last item
   * The last item
   * @param action
   */
  async enqueue<T>(action: () => Promise<T>): Promise<T> {
    strict.ok(!this.whenZero, 'items may not be added to the queue while it is being awaited');

    this.active++;
    this.total++;

    if (this.queue.length || this.active >= this.maxConcurency) {
      const result = new LazyPromise<T>(action);
      this.queue.push(result);
      return result;
    }

    return action().catch(async (e) => { this.rejections.push(e); throw e; }).finally(() => this.next());
  }

  enqueueMany<S, T>(array: Array<S>, fn: (v: S) => Promise<T>) {
    for (const each of array) {
      void this.enqueue(() => fn(each));
    }
    return this;
  }

}

