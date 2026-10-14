// sim/Transport.cpp - lossless item transport for belts and splitters.
//
// How it works (why belts are 100% efficient):
//
//  * Item progress is an integer number of ticks. Crossing one tile takes
//    exactly BELT_TICKS_PER_TILE ticks. Nothing is ever rounded.
//
//  * Each tick, every item first advances by one tick. Then the network is
//    processed DOWNSTREAM FIRST: a tile is always handled before the tiles
//    that feed it. So when an item leaves a tile, the tile behind it learns
//    about the free space in the same tick and refills it immediately. A
//    completely packed belt therefore moves as one unit and its output rate
//    is exactly 1 item every BELT_TICKS_PER_TILE ticks, however long it is.
//
//  * The order comes from the strongly connected components of the belt
//    graph (Tarjan's algorithm emits them sink-first, which is exactly
//    downstream-first). Belt loops are handled specially so that even a
//    completely full loop keeps rotating instead of jamming.
//
//  * Empty tiles PULL from their feeders with a round-robin pointer, which
//    makes merges perfectly fair and gap-free. Machines and splitters PUSH,
//    so a tile whose round-robin turn belongs to a machine/splitter keeps its
//    slot "reserved" for it, and the pusher fills it later in the same tick.
//
//  * Items are only ever moved (one place cleared, another filled in the
//    same statement block) - never created or destroyed here.
#include "World.hpp"
#include <algorithm>

namespace {
constexpr int T = BELT_TICKS_PER_TILE;
}

struct Transport {
    World& w;
    explicit Transport(World& world) : w(world) {}

    int beltCount() const { return int(w.belts_.size()); }
    int splitterNode(int machineId) const { return beltCount() + machineId; }

    static bool ready(const Belt& b) { return b.item != NO_ITEM && b.progress >= T; }
    static bool ready(const Machine& s) { return s.item != NO_ITEM && s.progress >= T; }

    // ------------------------------------------------------------------
    // Graph edges (only used to build the processing order). Uses the
    // neighbours cached by ensureTopology (frontRef / sideRef).
    // ------------------------------------------------------------------
    int edges(int node, int out[3]) {
        int n = 0;
        if (node < beltCount()) {
            const Belt& b = w.belts_[size_t(node)];
            if (!b.alive) return 0;
            TileRef r = b.frontRef;
            if (r > 0) {
                const Belt& y = w.belts_[size_t(r - 1)];
                if (b.dir != opposite(y.dir)) out[n++] = r - 1;
            } else if (r < 0) {
                const Machine& m = *w.machines_[size_t(-r - 1)];
                if (m.isSplitter() && m.dir == b.dir) out[n++] = splitterNode(-r - 1);
            }
        } else {
            int mid = node - beltCount();
            const Machine& s = *w.machines_[size_t(mid)];
            for (int i = 0; i < 3; ++i) {
                Dir sd = s.sideDir(i);
                TileRef r = s.sideRef[size_t(i)];
                if (r > 0) {
                    if (sd != opposite(w.belts_[size_t(r - 1)].dir)) out[n++] = r - 1;
                } else if (r < 0) {
                    const Machine& m2 = *w.machines_[size_t(-r - 1)];
                    if (m2.isSplitter() && m2.dir == sd) out[n++] = splitterNode(-r - 1);
                }
            }
        }
        return n;
    }

    void rebuild() {
        const int B = beltCount();
        const int N = B + int(w.machines_.size());
        std::vector<char> isNode(size_t(N), 0);
        for (int i = 0; i < B; ++i) isNode[size_t(i)] = w.belts_[size_t(i)].alive;
        w.producers_.clear();
        w.producingIds_.clear();
        for (int m = 0; m < int(w.machines_.size()); ++m) {
            if (!w.machineAlive(m)) continue;
            const Machine& mm = *w.machines_[size_t(m)];
            if (mm.isSplitter()) isNode[size_t(B + m)] = 1;
            else w.producers_.push_back(m);
            if (mm.isProducing()) w.producingIds_.push_back(m);
        }

        // Iterative Tarjan SCC.
        std::vector<int> index(size_t(N), -1), low(size_t(N), 0);
        std::vector<char> onStack(size_t(N), 0);
        std::vector<int> stack;
        struct Frame { int node; int edgeIdx; int nEdges; int e[3]; };
        std::vector<Frame> call;
        int counter = 0;
        w.order_.clear();
        w.sccs_.clear();

        for (int root = 0; root < N; ++root) {
            if (!isNode[size_t(root)] || index[size_t(root)] != -1) continue;
            Frame f{root, 0, 0, {0, 0, 0}};
            f.nEdges = edges(root, f.e);
            call.push_back(f);
            index[size_t(root)] = low[size_t(root)] = counter++;
            stack.push_back(root);
            onStack[size_t(root)] = 1;

            while (!call.empty()) {
                Frame& fr = call.back();
                if (fr.edgeIdx < fr.nEdges) {
                    int to = fr.e[fr.edgeIdx++];
                    if (index[size_t(to)] == -1) {
                        Frame nf{to, 0, 0, {0, 0, 0}};
                        nf.nEdges = edges(to, nf.e);
                        index[size_t(to)] = low[size_t(to)] = counter++;
                        stack.push_back(to);
                        onStack[size_t(to)] = 1;
                        call.push_back(nf);
                    } else if (onStack[size_t(to)]) {
                        low[size_t(fr.node)] = std::min(low[size_t(fr.node)], index[size_t(to)]);
                    }
                    continue;
                }
                int v = fr.node;
                if (low[size_t(v)] == index[size_t(v)]) {
                    int start = int(w.order_.size());
                    int x;
                    do {
                        x = stack.back();
                        stack.pop_back();
                        onStack[size_t(x)] = 0;
                        w.order_.push_back(x);
                    } while (x != v);
                    int len = int(w.order_.size()) - start;
                    uint8_t kind = 0;
                    if (len > 1) {
                        bool allBelts = true;
                        for (int i = start; i < start + len; ++i)
                            if (w.order_[size_t(i)] >= B) { allBelts = false; break; }
                        kind = allBelts ? 1 : 2;
                        if (kind == 1) orderCycle(start, len);
                    }
                    w.sccs_.push_back({start, len, kind});
                }
                call.pop_back();
                if (!call.empty()) {
                    int parent = call.back().node;
                    low[size_t(parent)] = std::min(low[size_t(parent)], low[size_t(v)]);
                }
            }
        }
    }

    // A strongly connected set of belts is always one simple loop (a belt
    // has a single output). Store it in travel order: c[i] feeds c[i+1].
    void orderCycle(int start, int len) {
        std::vector<int> cyc;
        cyc.reserve(size_t(len));
        int first = w.order_[size_t(start)];
        int cur = first;
        for (int i = 0; i < len; ++i) {
            cyc.push_back(cur);
            int e[3];
            edges(cur, e);
            cur = e[0];
        }
        for (int i = 0; i < len; ++i) w.order_[size_t(start + i)] = cyc[size_t(i)];
    }

    // ------------------------------------------------------------------
    // Moving items
    // ------------------------------------------------------------------
    void addInputAnim(Machine& m, ItemId item, Vec2i from, Dir d) {
        InputAnim a;
        a.item = item;
        a.sx = float(from.x * CELL_SIZE + CELL_SIZE / 2);
        a.sy = float(from.y * CELL_SIZE + CELL_SIZE / 2);
        Vec2i v = dirVec(d);
        a.ex = a.sx + float(v.x * CELL_SIZE);
        a.ey = a.sy + float(v.y * CELL_SIZE);
        if (m.anims.size() < 64) m.anims.push_back(a);
    }

    // Belt/splitter -> machine (smelter, assembler, storage). `d` = the
    // direction the item travels; smelters / assemblers refuse items coming
    // in through an output side (the item waits).
    bool insertIntoMachine(Machine& m, ItemId item, Vec2i from, Dir d) {
        if (m.isSplitter()) return false;
        if (machineHasOutputSides(m.type) && isOutputSide(m.type, m.dir, opposite(d))) return false;
        bool ok = false;
        if (m.isStorage()) ok = m.storage.tryAdd(item, 1);
        else if (MachineBuffer* buf = m.inputBuffer(item)) ok = buf->inv.tryAdd(item, 1);
        if (ok) addInputAnim(m, item, from, d);
        return ok;
    }

    static void giveToBelt(Belt& to, ItemId item, Dir travel) {
        to.item = item;
        to.progress = 0;
        to.itemFrom = travel;
        to.reservedFor = -1;
    }

    // Can producer `m` hand anything to belt `x` right now?
    static bool producerHasItemFor(const Machine& m, const Belt& x) {
        if (m.isStorage()) {
            for (auto& s : m.storage.slots())
                if (!s.empty() && x.filter.accepts(s.item)) return true;
            return false;
        }
        for (auto& b : m.outputs)
            if (b.inv.amount(b.item) > 0 && x.filter.accepts(b.item)) return true;
        return false;
    }

    static ItemId producerTakeItemFor(Machine& m, const Belt& x) {
        if (m.isStorage()) {
            Inventory& inv = m.storage;
            for (int y = 0; y < inv.height(); ++y)
                for (int xx = 0; xx < inv.width(); ++xx) {
                    const Slot& s = inv.at(xx, y);
                    if (!s.empty() && x.filter.accepts(s.item)) return inv.takeOneFromSlot(xx, y);
                }
            return NO_ITEM;
        }
        for (auto& b : m.outputs)
            if (b.inv.amount(b.item) > 0 && x.filter.accepts(b.item)) {
                b.inv.tryRemove(b.item, 1);
                return b.item;
            }
        return NO_ITEM;
    }

    // Empty belt X tries to take an item from one of its feeders, in
    // round-robin order. Returns true if anything changed.
    bool pullIntoBelt(Belt& X) {
        for (int k = 0; k < 4; ++k) {
            Dir d = Dir((X.rr + k) % 4);
            if (!(X.incomingMask & dirBit(d))) continue;
            TileRef r = X.feedRef[size_t(d)];
            if (r > 0) {
                Belt& N = w.belts_[size_t(r - 1)];
                if (N.dir == d && ready(N) && X.filter.accepts(N.item)) {
                    giveToBelt(X, N.item, d);
                    N.item = NO_ITEM;
                    N.progress = 0;
                    X.rr = uint8_t((int(d) + 1) % 4);
                    return true;
                }
            } else if (r < 0) {
                int mid = -r - 1;
                Machine& M = *w.machines_[size_t(mid)];
                if (M.isSplitter()) {
                    if (d == opposite(M.dir) || !ready(M)) continue;
                    int side = floorMod(int(d) - int(M.dir), 4) == 3 ? 0 : (d == M.dir ? 1 : 2);
                    if (M.sideFilters[size_t(side)].accepts(M.item) && X.filter.accepts(M.item)) {
                        X.reservedFor = mid;
                        return true;
                    }
                } else if (producerHasItemFor(M, X)) {
                    // (from behind or, for producing machines, from the side)
                    X.reservedFor = mid;
                    return true;
                }
            }
        }
        return false;
    }

    bool processBelt(int id) {
        Belt& X = w.belts_[size_t(id)];
        bool changed = false;
        if (ready(X)) {
            TileRef r = X.frontRef;
            if (r < 0) {
                Machine& m = *w.machines_[size_t(-r - 1)];
                if (!m.isSplitter() && insertIntoMachine(m, X.item, X.pos, X.dir)) {
                    X.item = NO_ITEM;
                    X.progress = 0;
                    changed = true;
                }
            }
            // A belt or splitter in front pulls the item itself.
        }
        if (X.item == NO_ITEM && X.reservedFor < 0) changed |= pullIntoBelt(X);
        return changed;
    }

    // Splitter S tries to hand its item to output side i. reservedOnly:
    // only belts/splitters that reserved this splitter, and machines.
    bool splitterDeliver(int sid, Machine& S, int side, bool reservedOnly) {
        Dir sd = S.sideDir(side);
        ItemId item = S.item;
        if (!S.sideFilters[size_t(side)].accepts(item)) return false;
        TileRef r = S.sideRef[size_t(side)];
        if (r > 0) {
            Belt& Y = w.belts_[size_t(r - 1)];
            if (Y.item != NO_ITEM || sd == opposite(Y.dir) || !Y.filter.accepts(item)) return false;
            if (reservedOnly ? Y.reservedFor != sid : (Y.reservedFor >= 0 && Y.reservedFor != sid)) return false;
            giveToBelt(Y, item, sd);
            Y.rr = uint8_t((int(sd) + 1) % 4);
            return true;
        }
        if (r < 0) {
            Machine& M = *w.machines_[size_t(-r - 1)];
            if (M.isSplitter()) {
                if (M.item != NO_ITEM || M.dir != sd) return false;
                if (reservedOnly ? M.reservedFor != sid : (M.reservedFor >= 0 && M.reservedFor != sid)) return false;
                M.item = item;
                M.progress = 0;
                M.reservedFor = -1;
                return true;
            }
            // A machine never reserves, but it is always "waiting" for the
            // splitter like a reserved belt, so it takes part in the first
            // round-robin pass. (Before, an empty belt on another side won
            // every time and the machine got nothing.)
            return insertIntoMachine(M, item, S.pos, sd);
        }
        return false;
    }

    bool processSplitter(int sid) {
        Machine& S = *w.machines_[size_t(sid)];
        bool changed = false;
        if (ready(S)) {
            int chosen = -1;
            for (int k = 0; k < 3 && chosen < 0; ++k) {
                int side = (S.outputIndex + k) % 3;
                if (splitterDeliver(sid, S, side, true)) chosen = side;
            }
            for (int k = 0; k < 3 && chosen < 0; ++k) {
                int side = (S.outputIndex + k) % 3;
                if (splitterDeliver(sid, S, side, false)) chosen = side;
            }
            if (chosen >= 0) {
                S.item = NO_ITEM;
                S.progress = 0;
                S.outputIndex = uint8_t((chosen + 1) % 3);
                changed = true;
            }
        }
        if (S.item == NO_ITEM && S.reservedFor < 0) {
            TileRef r = S.backRef;
            if (r > 0) {
                Belt& N = w.belts_[size_t(r - 1)];
                if (N.dir == S.dir && ready(N)) {
                    S.item = N.item;
                    S.progress = 0;
                    N.item = NO_ITEM;
                    N.progress = 0;
                    changed = true;
                }
            } else if (r < 0) {
                int mid = -r - 1;
                Machine& P = *w.machines_[size_t(mid)];
                if (P.isSplitter() && ready(P) && S.dir != opposite(P.dir)) {
                    int side = S.dir == P.dir ? 1 : (S.dir == rotateCW(P.dir, -1) ? 0 : 2);
                    if (P.sideFilters[size_t(side)].accepts(P.item)) {
                        S.reservedFor = mid;
                        changed = true;
                    }
                }
            }
        }
        return changed;
    }

    bool processNode(int node) {
        if (node < beltCount()) return processBelt(node);
        return processSplitter(node - beltCount());
    }

    void processCycle(int start, int len) {
        // c[i] feeds c[i+1]
        const int* c = &w.order_[size_t(start)];
        int breakAt = -1;
        for (int i = 0; i < len; ++i)
            if (!ready(w.belts_[size_t(c[i])])) { breakAt = i; break; }

        if (breakAt >= 0) {
            // The node at breakAt sends nothing this tick, so the loop can be
            // cut there and handled like a normal line, downstream first.
            for (int k = 0; k < len; ++k) {
                int i = floorMod(breakAt - k, len);
                processBelt(c[i]);
            }
            return;
        }

        // Every tile of the loop is full and ready: rotate everything by one
        // tile at the same time (if every filter allows it).
        for (int i = 0; i < len; ++i) {
            const Belt& from = w.belts_[size_t(c[i])];
            const Belt& to = w.belts_[size_t(c[(i + 1) % len])];
            if (!to.filter.accepts(from.item)) return;
        }
        ItemId carried = w.belts_[size_t(c[len - 1])].item;
        Dir carriedDir = w.belts_[size_t(c[len - 1])].dir;
        for (int i = len - 1; i >= 1; --i) {
            Belt& to = w.belts_[size_t(c[i])];
            const Belt& from = w.belts_[size_t(c[i - 1])];
            to.item = from.item;
            to.progress = 0;
            to.itemFrom = from.dir;
        }
        Belt& first = w.belts_[size_t(c[0])];
        first.item = carried;
        first.progress = 0;
        first.itemFrom = carriedDir;
    }

    void processComplex(int start, int len) {
        // Rare: loops that run through splitters. Repeat until nothing more
        // can move this tick (an item can still only move once, because a
        // freshly moved item has progress 0 and is not ready).
        for (int iter = 0; iter < 2 * len + 4; ++iter) {
            bool changed = false;
            for (int i = 0; i < len; ++i) changed |= processNode(w.order_[size_t(start + i)]);
            if (!changed) break;
        }
    }

    void pushFromProducers() {
        for (int mid : w.producers_) {
            Machine& M = *w.machines_[size_t(mid)];
            if (!M.alive) continue;
            bool any = false;
            if (M.isStorage()) any = !M.storage.isEmpty();
            else for (auto& b : M.outputs) if (b.inv.amount(b.item) > 0) { any = true; break; }
            if (!any) continue;
            for (size_t k = 0; k < M.outBelts.size(); ++k) {
                Belt& X = w.belts_[size_t(M.outBelts[k])];
                Dir travel = M.outDirs[k];
                if (X.item != NO_ITEM) continue;
                if (X.reservedFor >= 0 && X.reservedFor != mid) continue;
                bool wasReserved = X.reservedFor == mid;
                ItemId it = producerTakeItemFor(M, X);
                if (it == NO_ITEM) continue;
                giveToBelt(X, it, travel);
                if (wasReserved) X.rr = uint8_t((int(travel) + 1) % 4);
            }
        }
    }

    void tick() {
        // 1) every item advances one tick; scratch state is reset
        for (auto& b : w.belts_) {
            if (!b.alive) continue;
            b.reservedFor = -1;
            if (b.item != NO_ITEM && b.progress < T) ++b.progress;
        }
        for (int mid : w.splitterIds_) {
            Machine& s = *w.machines_[size_t(mid)];
            s.reservedFor = -1;
            if (s.item != NO_ITEM && s.progress < T) ++s.progress;
        }
        // 2) move items, downstream first
        for (const auto& scc : w.sccs_) {
            if (scc.kind == 0) processNode(w.order_[size_t(scc.start)]);
            else if (scc.kind == 1) processCycle(scc.start, scc.len);
            else processComplex(scc.start, scc.len);
        }
        // 3) machines fill the belt slots they reserved
        pushFromProducers();
    }
};

void World::rebuildTransportOrder() {
    Transport t(*this);
    t.rebuild();
}

void World::transportTick() {
    Transport t(*this);
    t.tick();
}

void World::machinesTick() {
    for (int mid : producingIds_) {
        Machine& m = *machines_[size_t(mid)];
        if (!m.alive || !m.recipe) continue;
        if (!m.processing && m.canProcess()) {
            m.processing = true;
            m.timer = 0;
        }
        if (m.processing) {
            ++m.timer;
            if (m.timer >= m.recipe->ticks) {
                // Re-check right before finishing: the player may have taken
                // inputs out or filled the output meanwhile. Waiting is
                // always safe; completing without inputs would create items.
                if (m.canProcess()) {
                    for (auto& p : m.recipe->inputs) m.inputBuffer(p.first)->inv.tryRemove(p.first, p.second);
                    for (auto& p : m.recipe->outputs) m.outputBuffer(p.first)->inv.tryAdd(p.first, p.second);
                    m.timer = 0;
                    if (!m.canProcess()) m.processing = false;
                } else {
                    m.timer = m.recipe->ticks;
                    bool inputsThere = true;
                    for (auto& p : m.recipe->inputs)
                        if (!m.inputBuffer(p.first) || m.inputBuffer(p.first)->inv.amount(p.first) < p.second) inputsThere = false;
                    if (!inputsThere) { m.processing = false; m.timer = 0; }
                }
            }
        }
    }
    for (int mid : producers_) {   // every machine that can get input animations
        auto& a = machines_[size_t(mid)]->anims;
        if (a.empty()) continue;
        for (auto& an : a) ++an.ticks;
        a.erase(std::remove_if(a.begin(), a.end(), [](const InputAnim& x) { return x.ticks >= x.duration; }), a.end());
    }
}
