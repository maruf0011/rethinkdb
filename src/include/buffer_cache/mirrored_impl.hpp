
#ifndef __BUFFER_CACHE_MIRRORED_IMPL_HPP__
#define __BUFFER_CACHE_MIRRORED_IMPL_HPP__

/**
 * Buffer implementation.
 */
template <class config_t>
mirrored_cache_t<config_t>::buf_t::buf_t(transaction_t *transaction,
        block_id_t block_id, void *data)
    : writeback_t::buf_t(),
      page_repl_t::buf_t(transaction->get_cache()),
      transaction(transaction),
      block_id(block_id),
      cached(false),
      data(data) {
}

template <class config_t>
void mirrored_cache_t<config_t>::buf_t::release(void *state) {
    /* XXX vvv This is incorrect. */
    if (this->is_dirty()) {
        aio_context_t *ctx = new aio_context_t();
        ctx->user_state = state;
        ctx->buf = this;
        ctx->block_id = block_id;
        transaction->get_cache()->do_write(get_cpu_context()->event_queue,
            block_id, ptr(), ctx);
        this->set_clean(); /* XXX XXX Can't do this until the I/O comes back! */
    }
    /* XXX ^^^ This is incorrect. */
    if (!this->is_dirty())
        this->unpin();
    ((concurrency_t*)(transaction->get_cache()))->release(this);

    // TODO: pinning/unpinning a block should come implicitly from
    // concurrency_t because it maintains all relevant reference
    // counts.
}

template <class config_t>
typename mirrored_cache_t<config_t>::node_t *
mirrored_cache_t<config_t>::buf_t::node() {
    /* TODO(NNW): Implement!! */
    assert(data);
    return (typename config_t::node_t *)data;
}

/**
 * Transaction implementation.
 */
template <class config_t>
mirrored_cache_t<config_t>::transaction_t::transaction_t(
        mirrored_cache_t *cache) : cache(cache), open(true) {
#ifndef NDEBUG
    event_queue = get_cpu_context()->event_queue;
#endif
}

template <class config_t>
mirrored_cache_t<config_t>::transaction_t::~transaction_t() {
    assert(!open);
}

template <class config_t>
void mirrored_cache_t<config_t>::transaction_t::commit() {
    /* TODO(NNW): Implement!! */
    /* XXX This should only actually occur when the commit is complete, not
     * merely when it has been started. */
    open = false;
}

template <class config_t>
typename mirrored_cache_t<config_t>::buf_t *
mirrored_cache_t<config_t>::transaction_t::allocate(block_id_t *block_id) {
    assert(event_queue == get_cpu_context()->event_queue);
        
    *block_id = cache->gen_block_id();
    buf_t *buf = new buf_t(this, *block_id,
                           cache->malloc(((serializer_t *)cache)->block_size));
    cache->set(*block_id, buf);
    buf->pin();
    ((concurrency_t*)cache)->acquire(buf, rwi_write);
        
    return buf;
}

template <class config_t>
typename mirrored_cache_t<config_t>::buf_t *
mirrored_cache_t<config_t>::transaction_t::acquire(block_id_t block_id,
                                                   void *state,
                                                   access_t mode) {
    assert(event_queue == get_cpu_context()->event_queue);
       
    // TODO: we might get a request for a block id while the block
    // with that block id is still loading (consider two requests
    // in a row). We need to keep track of this so we don't
    // unnecessarily double IO and/or lose memory.

    buf_t *buf = (buf_t *)cache->find(block_id);
    if (!buf) {
        buf_t *buf;

        buf = new buf_t(this, block_id,
                        cache->malloc(((serializer_t *)cache)->block_size));
        ((concurrency_t*)cache)->acquire(buf, mode);
        assert(buf->ptr()); /* XXX */
        ((mirrored_cache_t::page_map_t *)cache)->set(block_id, buf);
        aio_context_t *ctx = new aio_context_t();
        ctx->buf = buf;
        ctx->user_state = state;
        ctx->block_id = block_id;
#ifndef NDEBUG            
        ctx->event_queue = get_cpu_context()->event_queue;
#endif

        cache->do_read(get_cpu_context()->event_queue, block_id, buf->ptr(), ctx);
    } else {
        buf->pin();
        ((concurrency_t*)cache)->acquire(buf, mode);
        if (!buf->is_cached()) { /* The data is not yet ready, queue us up. */
            /* XXX Add us to waiters queue; maybe lock code handles this? */
            buf = NULL;
        }
    }

    return buf;
}

/**
 * Cache implementation.
 */
template <class config_t>
typename mirrored_cache_t<config_t>::transaction_t *
mirrored_cache_t<config_t>::begin_transaction() {
    transaction_t *txn = new transaction_t(this);
    return txn;
}

template <class config_t>
void mirrored_cache_t<config_t>::aio_complete(aio_context_t *ctx,
        void *block, bool written) {
#ifndef NDEBUG            
    assert(ctx->event_queue = get_cpu_context()->event_queue);
#endif

    buf_t *buf = ctx->buf;
    buf->set_cached(true);
    delete ctx;
    if(written) {
        buf->unpin();
    } else {
        buf->pin();
    }
}

#endif  // __BUFFER_CACHE_MIRRORED_IMPL_HPP__
