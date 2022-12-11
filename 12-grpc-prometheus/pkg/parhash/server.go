package parhash

import (
	"context"
    "net"
	"sync"
    "log"
    "time"

	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"
	"golang.org/x/sync/semaphore"
    "google.golang.org/grpc"

	parhashpb "fs101ex/pkg/gen/parhashsvc"
    hashpb "fs101ex/pkg/gen/hashsvc"
	"fs101ex/pkg/workgroup"
)

type Config struct {
	ListenAddr   string
	BackendAddrs []string
	Concurrency  int

	Prom prometheus.Registerer
}

// Implement a server that responds to ParallelHash()
// as declared in /proto/parhash.proto.
//
// The implementation of ParallelHash() must not hash the content
// of buffers on its own. Instead, it must send buffers to backends
// to compute hashes. Buffers must be fanned out to backends in the
// round-robin fashion.
//
// For example, suppose that 2 backends are configured and ParallelHash()
// is called to compute hashes of 5 buffers. In this case it may assign
// buffers to backends in this way:
//
//	backend 0: buffers 0, 2, and 4,
//	backend 1: buffers 1 and 3.
//
// Requests to hash individual buffers must be issued concurrently.
// Goroutines that issue them must run within /pkg/workgroup/Wg. The
// concurrency within workgroups must be limited by Server.sem.
//
// WARNING: requests to ParallelHash() may be concurrent, too.
// Make sure that the round-robin fanout works in that case too,
// and evenly distributes the load across backends.
//
// The server must report the following performance counters to Prometheus:
//
//  1. nr_nr_requests: a counter that is incremented every time a call
//     is made to ParallelHash(),
//
//  2. subquery_durations: a histogram that tracks durations of calls
//     to backends.
//     It must have a label `backend`.
//     Each subquery_durations{backed=backend_addr} must be a histogram
//     with 24 exponentially growing buckets ranging from 0.1ms to 10s.
//
// Both performance counters must be placed to Prometheus namespace "parhash".
type Server struct {
	conf Config
	sem *semaphore.Weighted
    stop context.CancelFunc
	l    net.Listener
	wg   sync.WaitGroup
    lock   sync.Mutex
    RRn int
    nr_nr_requests prometheus.Counter
    subquery_durations *prometheus.HistogramVec
}

func New(conf Config) *Server {
	return &Server{
		conf: conf,
		sem:  semaphore.NewWeighted(int64(conf.Concurrency)),
        nr_nr_requests: prometheus.NewCounter(prometheus.CounterOpts{
			Namespace: "parhash",
			Name: "nr_nr_requests",
		}),
		subquery_durations: prometheus.NewHistogramVec(prometheus.HistogramOpts{
			Namespace: "parhash",
			Name: "subquery_durations",
			Buckets: prometheus.ExponentialBuckets(0.1, 10000, 24),
		}, []string{"backend"}),

	}
}

func (s *Server) Start(ctx context.Context) (err error) {
	defer func() { err = errors.Wrap(err, "Start()") }()

	ctx, s.stop = context.WithCancel(ctx)

	s.l, err = net.Listen("tcp", s.conf.ListenAddr)
	if err != nil {
		return err
	}
    s.RRn = 0
	srv := grpc.NewServer()
	parhashpb.RegisterParallelHashSvcServer(srv, s)

	s.wg.Add(2)
	go func() {
		defer s.wg.Done()

		srv.Serve(s.l)
	}()
	go func() {
		defer s.wg.Done()

		<-ctx.Done()
		s.l.Close()
	}()

    s.conf.Prom.MustRegister(s.nr_nr_requests)
	s.conf.Prom.MustRegister(s.subquery_durations)
	return nil
}

func (s *Server) ListenAddr() string {
	return s.l.Addr().String()
}

func (s *Server) Stop() {
	s.stop()
	s.wg.Wait()
}

func (s *Server) ParallelHash(ctx context.Context, req *parhashpb.ParHashReq) (resp *parhashpb.ParHashResp, err error) {
    
    s.lock.Lock()
    s.nr_nr_requests.Inc()
    s.lock.Unlock()
    count := len(s.conf.BackendAddrs)
    var (
	    client = make([] hashpb.HashSvcClient, count)
	    conn = make([] *grpc.ClientConn, count)
    )
    for i := range conn {
	    conn[i], err = grpc.Dial(s.conf.BackendAddrs[i],
		    grpc.WithInsecure(), /* allow non-TLS connections */
	    )
	    if err != nil {
		    log.Fatalf("failed to connect to %q: %v", s.conf.BackendAddrs[i], err)
	    }
	    defer conn[i].Close()

	    client[i] = hashpb.NewHashSvcClient(conn[i])
    }

	var (
		wg     = workgroup.New(workgroup.Config{Sem: s.sem})
		hashes = make([][] byte, len(req.Data))
        lock   sync.Mutex
	)
    
	for i := range req.Data {
        ctx_i := i
		wg.Go(ctx, func(ctx context.Context) (error) {
			s.lock.Lock()
            handler := s.RRn
			s.RRn = (s.RRn + 1) % count
            s.lock.Unlock()
            start := time.Now()
			resp, err := client[handler].Hash(ctx, &hashpb.HashReq{Data: req.Data[ctx_i]})
			end := time.Now()
            elapsed := end.Sub(start)
            if err != nil {
				return err
			}
			lock.Lock()
			hashes[ctx_i] = resp.Hash
			lock.Unlock()
            s.subquery_durations.GetMetricWith(prometheus.Labels{"backend":s.conf.BackendAddrs[handler]}).Observe(float64(elapsed.Milliseconds()))
			return nil
		})
	}
	if err := wg.Wait(); err != nil {
		log.Fatalf("failed to hash files: %v", err)
	}

	return &parhashpb.ParHashResp{Hashes: hashes}, nil
}
