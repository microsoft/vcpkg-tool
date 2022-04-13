// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

/**
* A manually (or externally) controlled asynchronous Promise implementation
*/
export class ManualPromise<T> implements Promise<T> {
  /**
    * Attaches callbacks for the resolution and/or rejection of the Promise.
    * @param onfulfilled The callback to execute when the Promise is resolved.
    * @param onrejected The callback to execute when the Promise is rejected.
    * @returns A Promise for the completion of which ever callback is executed.
    */
  then<TResult1 = T, TResult2 = never>(onfulfilled?: ((value: T) => TResult1 | PromiseLike<TResult1>) | null | undefined, onrejected?: ((reason: any) => TResult2 | PromiseLike<TResult2>) | null | undefined): Promise<TResult1 | TResult2> {
    return this.p.then(onfulfilled, onrejected);
  }
  /**
  * Attaches a callback for only the rejection of the Promise.
  * @param onrejected The callback to execute when the Promise is rejected.
  * @returns A Promise for the completion of the callback.
  */
  catch<TResult = never>(onrejected?: ((reason: any) => TResult | PromiseLike<TResult>) | null | undefined): Promise<T | TResult> {
    return this.p.catch(onrejected);
  }
  finally(onfinally?: (() => void) | null | undefined): Promise<T> {
    return this.p.finally(onfinally);
  }

  readonly [Symbol.toStringTag]: 'Promise';
  private p: Promise<T>;

  /**
   * A method to manually resolve the Promise.
   */
  public resolve: (value?: T | PromiseLike<T> | undefined) => void = (v) => { /* */ };

  /**
   *  A method to manually reject the Promise
   */
  public reject: (e: any) => void = (e) => { /* */ };

  private state: 'pending' | 'resolved' | 'rejected' = 'pending';

  /**
   * Returns true of the Promise has been Resolved or Rejected
   */
  public get isCompleted(): boolean {
    return this.state !== 'pending';
  }

  /**
   * Returns true if the Promise has been Resolved.
   */
  public get isResolved(): boolean {
    return this.state === 'resolved';
  }

  /**
   * Returns true if the Promise has been Rejected.
   */
  public get isRejected(): boolean {
    return this.state === 'rejected';
  }

  public constructor() {
    this.p = new Promise<T>((r, j) => {
      this.resolve = (v: T | PromiseLike<T> | undefined) => { this.state = 'resolved'; r(<any>v); };
      this.reject = (e: any) => { this.state = 'rejected'; j(e); };
    });
  }
}

export class LazyPromise<T> extends ManualPromise<T> {
  public constructor(private action: () => Promise<T>) {
    super();
  }

  execute() {
    this.action().then(v => this.resolve(v), e => this.reject(e));
    return this;
  }
}
