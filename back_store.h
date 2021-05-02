struct bsframe{
    int va;
    uint next_index;
};

struct {
    struct spinlock lock;
    struct bsframe back_store_allocation[BACKSTORE_SIZE/8];
}back_store;
