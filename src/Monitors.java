public class Monitors {

    static final class Event {
        final int id;
        final String payload;

        Event(int id) {
            this.id = id;
            this.payload = "payload #" + id;
        }
    }

    static final class Monitor {
        private boolean ready = false;
        private Event data = null;

        synchronized boolean provide(Event e) {
            if (ready) {
                System.out.println("provider: событие " + e.id + " ждёт, предыдущее не обработано");
                return false;
            }
            data = e;
            ready = true;
            System.out.println("provider: отправлено событие " + e.id);
            notify();
            return true;
        }

        synchronized Event consume() throws InterruptedException {
            while (!ready) {
                wait();
            }
            Event e = data;
            data = null;
            ready = false;
            System.out.println("consumer: получено событие " + e.id + " (" + e.payload + ")");
            return e;
        }
    }

    public static void main(String[] args) {
        Monitor monitor = new Monitor();

        Thread provider = new Thread(() -> {
            Event pending = null;
            try {
                for (int id = 1; ; ) {
                    Thread.sleep(1000);
                    if (pending == null) {
                        pending = new Event(id++);
                    }
                    if (monitor.provide(pending)) {
                        pending = null;
                    }
                }
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
            }
        }, "provider");

        Thread consumer = new Thread(() -> {
            try {
                for (;;) {
                    monitor.consume();
                }
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
            }
        }, "consumer");

        consumer.start();
        provider.start();
    }
}