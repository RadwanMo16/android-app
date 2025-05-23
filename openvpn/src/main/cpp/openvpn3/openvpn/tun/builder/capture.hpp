//    OpenVPN -- An application to securely tunnel IP networks
//               over a single port, with support for SSL/TLS-based
//               session authentication and key exchange,
//               packet encryption, packet authentication, and
//               packet compression.
//
//    Copyright (C) 2012- OpenVPN Inc.
//
//    SPDX-License-Identifier: MPL-2.0 OR AGPL-3.0-only WITH openvpn3-openssl-exception
//

// An artificial TunBuilder object, used to log the tun builder settings,
// but doesn't actually configure anything.

#ifndef OPENVPN_TUN_BUILDER_CAPTURE_H
#define OPENVPN_TUN_BUILDER_CAPTURE_H

#include <string>
#include <sstream>
#include <vector>

#include <openvpn/common/exception.hpp>
#include <openvpn/common/rc.hpp>
#include <openvpn/common/size.hpp>
#include <openvpn/common/hostport.hpp>
#include <openvpn/common/to_string.hpp>
#include <openvpn/common/jsonlib.hpp>
#include <openvpn/tun/builder/base.hpp>
#include <openvpn/client/rgopt.hpp>
#include <openvpn/client/dns.hpp>
#include <openvpn/addr/ip.hpp>
#include <openvpn/addr/route.hpp>
#include <openvpn/http/urlparse.hpp>
#include <openvpn/tun/layer.hpp>

#ifdef HAVE_JSON
#include <openvpn/common/jsonhelper.hpp>
#endif

namespace openvpn {
class TunBuilderCapture : public TunBuilderBase, public RC<thread_unsafe_refcount>
{
  public:
    using Ptr = RCPtr<TunBuilderCapture>;

    // builder data classes

    class RemoteAddress
    {
      public:
        std::string address;
        bool ipv6 = false;

        std::string to_string() const
        {
            std::string ret = address;
            if (ipv6)
                ret += " [IPv6]";
            return ret;
        }

        bool defined() const
        {
            return !address.empty();
        }

        void validate(const std::string &title) const
        {
            IP::Addr(address, title, ipv6 ? IP::Addr::V6 : IP::Addr::V4);
        }

#ifdef HAVE_JSON
        Json::Value to_json() const
        {
            Json::Value root(Json::objectValue);
            root["address"] = Json::Value(address);
            root["ipv6"] = Json::Value(ipv6);
            return root;
        }

        void from_json(const Json::Value &root, const std::string &title)
        {
            if (!json::is_dict(root, title))
                return;
            json::to_string(root, address, "address", title);
            json::to_bool(root, ipv6, "ipv6", title);
        }
#endif
    };

    class RerouteGW
    {
      public:
        bool ipv4 = false;
        bool ipv6 = false;
        unsigned int flags = 0;

        std::string to_string() const
        {
            std::ostringstream os;
            const RedirectGatewayFlags rgf(flags);
            os << "IPv4=" << ipv4 << " IPv6=" << ipv6 << " flags=" << rgf.to_string();
            return os.str();
        }


#ifdef HAVE_JSON
        Json::Value to_json() const
        {
            Json::Value root(Json::objectValue);
            root["ipv4"] = Json::Value(ipv4);
            root["ipv6"] = Json::Value(ipv6);
            root["flags"] = Json::Value(flags);
            return root;
        }

        void from_json(const Json::Value &root, const std::string &title)
        {
            json::assert_dict(root, title);
            json::to_bool(root, ipv4, "ipv4", title);
            json::to_bool(root, ipv6, "ipv6", title);
            json::to_uint(root, flags, "flags", title);
        }
#endif
    };

    class RouteBase
    {
      public:
        std::string address;
        unsigned char prefix_length = 0;
        int metric = -1;     // optional
        std::string gateway; // optional
        bool ipv6 = false;
        bool net30 = false;

        std::string to_string() const
        {
            std::ostringstream os;
            os << address << '/' << static_cast<uint16_t>(prefix_length);
            if (!gateway.empty())
                os << " -> " << gateway;
            if (metric >= 0)
                os << " [METRIC=" << metric << ']';
            if (ipv6)
                os << " [IPv6]";
            if (net30)
                os << " [net30]";
            return os.str();
        }

#ifdef HAVE_JSON
        Json::Value to_json() const
        {
            Json::Value root(Json::objectValue);
            root["address"] = Json::Value(address);
            root["prefix_length"] = Json::Value(prefix_length);
            root["metric"] = Json::Value(metric);
            root["gateway"] = Json::Value(gateway);
            root["ipv6"] = Json::Value(ipv6);
            root["net30"] = Json::Value(net30);
            return root;
        }

        void from_json(const Json::Value &root, const std::string &title)
        {
            json::assert_dict(root, title);
            json::to_string(root, address, "address", title);
            json::to_uchar(root, prefix_length, "prefix_length", title);
            json::to_int(root, metric, "metric", title);
            json::to_string(root, gateway, "gateway", title);
            json::to_bool(root, ipv6, "ipv6", title);
            json::to_bool(root, net30, "net30", title);
        }
#endif

      protected:
        static constexpr int net30_prefix_length = 30;

        void validate_(const std::string &title, const bool require_canonical) const
        {
            const IP::Addr::Version ver = ipv6 ? IP::Addr::V6 : IP::Addr::V4;
            const IP::Route route = IP::route_from_string_prefix(address, prefix_length, title, ver);
            if (require_canonical && !route.is_canonical())
                OPENVPN_THROW_EXCEPTION(title << " : not a canonical route: " << route);
            if (!gateway.empty())
                IP::Addr(gateway, title + ".gateway", ver);
            if (net30 && route.prefix_len != net30_prefix_length)
                OPENVPN_THROW_EXCEPTION(title << " : not a net30 route: " << route);
        }
    };

    class RouteAddress : public RouteBase // may be non-canonical
    {
      public:
        void validate(const std::string &title) const
        {
            validate_(title, false);
        }
    };

    class Route : public RouteBase // must be canonical
    {
      public:
        void validate(const std::string &title) const
        {
            validate_(title, true);
        }
    };

    class ProxyBypass
    {
      public:
        std::string bypass_host;

        std::string to_string() const
        {
            return bypass_host;
        }

        bool defined() const
        {
            return !bypass_host.empty();
        }

        void validate(const std::string &title) const
        {
            if (defined())
                HostPort::validate_host(bypass_host, title);
        }

#ifdef HAVE_JSON
        Json::Value to_json() const
        {
            Json::Value root(Json::objectValue);
            root["bypass_host"] = Json::Value(bypass_host);
            return root;
        }

        void from_json(const Json::Value &root, const std::string &title)
        {
            json::assert_dict(root, title);
            json::to_string(root, bypass_host, "bypass_host", title);
        }
#endif
    };

    class ProxyAutoConfigURL
    {
      public:
        std::string url;

        std::string to_string() const
        {
            return url;
        }

        bool defined() const
        {
            return !url.empty();
        }

        void validate(const std::string &title) const
        {
            try
            {
                if (defined())
                    (URL::Parse(url));
            }
            catch (const std::exception &e)
            {
                OPENVPN_THROW_EXCEPTION(title << " : error parsing ProxyAutoConfigURL: " << e.what());
            }
        }

#ifdef HAVE_JSON
        Json::Value to_json() const
        {
            Json::Value root(Json::objectValue);
            root["url"] = Json::Value(url);
            return root;
        }

        void from_json(const Json::Value &root, const std::string &title)
        {
            if (!json::is_dict(root, title))
                return;
            json::to_string(root, url, "url", title);
        }
#endif
    };

    class ProxyHostPort
    {
      public:
        std::string host;
        int port = 0;

        std::string to_string() const
        {
            std::ostringstream os;
            os << host << ' ' << port;
            return os.str();
        }

        bool defined() const
        {
            return !host.empty();
        }

        void validate(const std::string &title) const
        {
            if (defined())
            {
                HostPort::validate_host(host, title);
                HostPort::validate_port(port, title);
            }
        }

#ifdef HAVE_JSON
        Json::Value to_json() const
        {
            Json::Value root(Json::objectValue);
            root["host"] = Json::Value(host);
            root["port"] = Json::Value(port);
            return root;
        }

        void from_json(const Json::Value &root, const std::string &title)
        {
            if (!json::is_dict(root, title))
                return;
            json::to_string(root, host, "host", title);
            json::to_int(root, port, "port", title);
        }
#endif
    };

    class WINSServer
    {
      public:
        std::string address;

        std::string to_string() const
        {
            std::string ret = address;
            return ret;
        }

        void validate(const std::string &title) const
        {
            IP::Addr(address, title, IP::Addr::V4);
        }

#ifdef HAVE_JSON
        Json::Value to_json() const
        {
            Json::Value root(Json::objectValue);
            root["address"] = Json::Value(address);
            return root;
        }

        void from_json(const Json::Value &root, const std::string &title)
        {
            json::assert_dict(root, title);
            json::to_string(root, address, "address", title);
        }
#endif
    };

    bool tun_builder_set_remote_address(const std::string &address, bool ipv6) override
    {
        remote_address.address = address;
        remote_address.ipv6 = ipv6;
        return true;
    }

    bool tun_builder_add_address(const std::string &address, int prefix_length, const std::string &gateway, bool ipv6, bool net30) override
    {
        RouteAddress r;
        r.address = address;
        r.prefix_length = static_cast<unsigned char>(prefix_length);
        r.gateway = gateway;
        r.ipv6 = ipv6;
        r.net30 = net30;
        if (ipv6)
            tunnel_address_index_ipv6 = static_cast<int>(tunnel_addresses.size());
        else
            tunnel_address_index_ipv4 = static_cast<int>(tunnel_addresses.size());
        tunnel_addresses.push_back(std::move(r));
        return true;
    }

    bool tun_builder_reroute_gw(bool ipv4, bool ipv6, unsigned int flags) override
    {
        reroute_gw.ipv4 = ipv4;
        reroute_gw.ipv6 = ipv6;
        reroute_gw.flags = flags;
        return true;
    }

    bool tun_builder_set_route_metric_default(int metric) override
    {
        route_metric_default = metric;
        return true;
    }

    bool tun_builder_add_route(const std::string &address, int prefix_length, int metric, bool ipv6) override
    {
        Route r;
        r.address = address;
        r.prefix_length = static_cast<unsigned char>(prefix_length);
        r.metric = (metric < 0 ? route_metric_default : metric);
        r.ipv6 = ipv6;
        add_routes.push_back(std::move(r));
        return true;
    }

    bool tun_builder_exclude_route(const std::string &address, int prefix_length, int metric, bool ipv6) override
    {
        Route r;
        r.address = address;
        r.prefix_length = static_cast<unsigned char>(prefix_length);
        r.metric = metric;
        r.ipv6 = ipv6;
        exclude_routes.push_back(std::move(r));
        return true;
    }

    /**
     * @brief Set DNS options for use with tun builder
     *
     * Calling this invalidates any DNS related --dhcp-options previously added.
     *
     * @param dns       The --dns options to be set
     * @return true     unconditionally
     */
    bool tun_builder_set_dns_options(const DnsOptions &dns) override
    {
        dns_options = dns;
        return true;
    }

    bool tun_builder_set_layer(int layer) override
    {
        this->layer = Layer::from_value(layer);
        return true;
    }

    bool tun_builder_set_mtu(int mtu) override
    {
        this->mtu = mtu;
        return true;
    }

    bool tun_builder_set_session_name(const std::string &name) override
    {
        session_name = name;
        return true;
    }

    bool tun_builder_add_proxy_bypass(const std::string &bypass_host) override
    {
        ProxyBypass b;
        b.bypass_host = bypass_host;
        proxy_bypass.push_back(std::move(b));
        return true;
    }

    bool tun_builder_set_proxy_auto_config_url(const std::string &url) override
    {
        proxy_auto_config_url.url = url;
        return true;
    }

    bool tun_builder_set_proxy_http(const std::string &host, int port) override
    {
        http_proxy.host = host;
        http_proxy.port = port;
        return true;
    }

    bool tun_builder_set_proxy_https(const std::string &host, int port) override
    {
        https_proxy.host = host;
        https_proxy.port = port;
        return true;
    }

    bool tun_builder_add_wins_server(const std::string &address) override
    {
        WINSServer wins;
        wins.address = address;
        wins_servers.push_back(std::move(wins));
        return true;
    }

    bool tun_builder_set_allow_family(int af, bool allow) override
    {
        if (af == AF_INET)
            block_ipv4 = !allow;
        else if (af == AF_INET6)
            block_ipv6 = !allow;
        return true;
    }

    bool tun_builder_set_allow_local_dns(bool allow) override
    {
        block_outside_dns = !allow;
        return true;
    }

    void reset_tunnel_addresses()
    {
        tunnel_addresses.clear();
        tunnel_address_index_ipv4 = -1;
        tunnel_address_index_ipv6 = -1;
    }

    void reset_dns_options()
    {
        dns_options = {};
    }

    const RouteAddress *vpn_ipv4() const
    {
        if (tunnel_address_index_ipv4 >= 0)
            return &tunnel_addresses[tunnel_address_index_ipv4];
        return nullptr;
    }

    const RouteAddress *vpn_ipv6() const
    {
        if (tunnel_address_index_ipv6 >= 0)
            return &tunnel_addresses[tunnel_address_index_ipv6];
        return nullptr;
    }

    const RouteAddress *vpn_ip(const IP::Addr::Version v) const
    {
        switch (v)
        {
        case IP::Addr::V4:
            return vpn_ipv4();
        case IP::Addr::V6:
            return vpn_ipv6();
        default:
            return nullptr;
        }
    }

    void validate() const
    {
        validate_layer("root");
        validate_mtu("root");
        remote_address.validate("remote_address");
        validate_list(tunnel_addresses, "tunnel_addresses");
        validate_tunnel_address_indices("root");
        validate_list(add_routes, "add_routes");
        validate_list(exclude_routes, "exclude_routes");
        validate_list(proxy_bypass, "proxy_bypass");
        proxy_auto_config_url.validate("proxy_auto_config_url");
        http_proxy.validate("http_proxy");
        https_proxy.validate("https_proxy");
    }

    std::string to_string() const
    {
        std::ostringstream os;
        os << "Session Name: " << session_name << '\n';
        os << "Layer: " << layer.str() << '\n';
        if (mtu)
            os << "MTU: " << mtu << '\n';
        os << "Remote Address: " << remote_address.to_string() << '\n';
        render_list(os, "Tunnel Addresses", tunnel_addresses);
        os << "Reroute Gateway: " << reroute_gw.to_string() << '\n';
        os << "Block IPv4: " << (block_ipv4 ? "yes" : "no") << '\n';
        os << "Block IPv6: " << (block_ipv6 ? "yes" : "no") << '\n';
        os << "Block local DNS: " << (block_outside_dns ? "yes" : "no") << '\n';
        if (route_metric_default >= 0)
            os << "Route Metric Default: " << route_metric_default << '\n';
        render_list(os, "Add Routes", add_routes);
        render_list(os, "Exclude Routes", exclude_routes);
        if (!dns_options.servers.empty())
        {
            os << dns_options.to_string() << '\n';
        }
        if (!proxy_bypass.empty())
            render_list(os, "Proxy Bypass", proxy_bypass);
        if (proxy_auto_config_url.defined())
            os << "Proxy Auto Config URL: " << proxy_auto_config_url.to_string() << '\n';
        if (http_proxy.defined())
            os << "HTTP Proxy: " << http_proxy.to_string() << '\n';
        if (https_proxy.defined())
            os << "HTTPS Proxy: " << https_proxy.to_string() << '\n';
        if (!wins_servers.empty())
            render_list(os, "WINS Servers", wins_servers);
        return os.str();
    }

#ifdef HAVE_JSON

    Json::Value to_json() const
    {
        Json::Value root(Json::objectValue);
        root["session_name"] = Json::Value(session_name);
        root["mtu"] = Json::Value(mtu);
        root["layer"] = Json::Value(layer.value());
        if (remote_address.defined())
            root["remote_address"] = remote_address.to_json();
        json::from_vector(root, tunnel_addresses, "tunnel_addresses");
        root["tunnel_address_index_ipv4"] = Json::Value(tunnel_address_index_ipv4);
        root["tunnel_address_index_ipv6"] = Json::Value(tunnel_address_index_ipv6);
        root["reroute_gw"] = reroute_gw.to_json();
        root["block_ipv6"] = Json::Value(block_ipv6);
        root["block_outside_dns"] = Json::Value(block_outside_dns);
        root["route_metric_default"] = Json::Value(route_metric_default);
        json::from_vector(root, add_routes, "add_routes");
        json::from_vector(root, exclude_routes, "exclude_routes");
        root["dns_options"] = dns_options.to_json();
        json::from_vector(root, wins_servers, "wins_servers");
        json::from_vector(root, proxy_bypass, "proxy_bypass");
        if (proxy_auto_config_url.defined())
            root["proxy_auto_config_url"] = proxy_auto_config_url.to_json();
        if (http_proxy.defined())
            root["http_proxy"] = http_proxy.to_json();
        if (https_proxy.defined())
            root["https_proxy"] = https_proxy.to_json();
        return root;
    }

    static TunBuilderCapture::Ptr from_json(const Json::Value &root)
    {
        const std::string title = "root";
        TunBuilderCapture::Ptr tbc(new TunBuilderCapture);
        json::assert_dict(root, title);
        json::to_string(root, tbc->session_name, "session_name", title);
        tbc->layer = Layer::from_value(json::get_int(root, "layer", title));
        json::to_int(root, tbc->mtu, "mtu", title);
        tbc->remote_address.from_json(root["remote_address"], "remote_address");
        json::to_vector(root, tbc->tunnel_addresses, "tunnel_addresses", title);
        json::to_int(root, tbc->tunnel_address_index_ipv4, "tunnel_address_index_ipv4", title);
        json::to_int(root, tbc->tunnel_address_index_ipv6, "tunnel_address_index_ipv6", title);
        tbc->reroute_gw.from_json(root["reroute_gw"], "reroute_gw");
        json::to_bool(root, tbc->block_ipv6, "block_ipv6", title);
        json::to_bool(root, tbc->block_outside_dns, "block_outside_dns", title);
        json::to_int(root, tbc->route_metric_default, "route_metric_default", title);
        json::to_vector(root, tbc->add_routes, "add_routes", title);
        json::to_vector(root, tbc->exclude_routes, "exclude_routes", title);
        tbc->dns_options.from_json(root["dns_options"], "dns_options");
        json::to_vector(root, tbc->wins_servers, "wins_servers", title);
        json::to_vector(root, tbc->proxy_bypass, "proxy_bypass", title);
        tbc->proxy_auto_config_url.from_json(root["proxy_auto_config_url"], "proxy_auto_config_url");
        tbc->http_proxy.from_json(root["http_proxy"], "http_proxy");
        tbc->https_proxy.from_json(root["https_proxy"], "https_proxy");
        return tbc;
    }

#endif // HAVE_JSON

    // builder data
    std::string session_name;
    int mtu = 0;
    Layer layer{Layer::OSI_LAYER_3};            // OSI layer
    RemoteAddress remote_address;               // real address of server
    std::vector<RouteAddress> tunnel_addresses; // local tunnel addresses
    int tunnel_address_index_ipv4 = -1;         // index into tunnel_addresses for IPv4 entry (or -1 if undef)
    int tunnel_address_index_ipv6 = -1;         // index into tunnel_addresses for IPv6 entry (or -1 if undef)
    RerouteGW reroute_gw;                       // redirect-gateway info
    bool block_ipv4 = false;                    // block IPv4 traffic while VPN is active
    bool block_ipv6 = false;                    // block IPv6 traffic while VPN is active
    bool block_outside_dns = false;             // block traffic to port 53 locally while VPN is active
    int route_metric_default = -1;              // route-metric directive
    std::vector<Route> add_routes;              // routes that should be added to tunnel
    std::vector<Route> exclude_routes;          // routes that should be excluded from tunnel
    DnsOptions dns_options;                     // VPN DNS related settings from --dns option

    std::vector<ProxyBypass> proxy_bypass; // hosts that should bypass proxy
    ProxyAutoConfigURL proxy_auto_config_url;
    ProxyHostPort http_proxy;
    ProxyHostPort https_proxy;

    std::vector<WINSServer> wins_servers; // Windows WINS servers

    static constexpr int mtu_ipv4_maximum = 65'535;

  private:
    template <typename LIST>
    static void render_list(std::ostream &os, const std::string &title, const LIST &list)
    {
        os << title << ':' << '\n';
        for (auto &e : list)
        {
            os << "  " << e.to_string() << '\n';
        }
    }

    template <typename LIST>
    static void validate_list(const LIST &list, const std::string &title)
    {
        int i = 0;
        for (auto &e : list)
        {
            e.validate(title + '[' + openvpn::to_string(i) + ']');
            ++i;
        }
    }

    bool validate_tunnel_index(const int index) const
    {
        if (index == -1)
            return true;
        return index >= 0 && static_cast<unsigned int>(index) <= tunnel_addresses.size();
    }

    void validate_tunnel_address_indices(const std::string &title) const
    {
        if (!validate_tunnel_index(tunnel_address_index_ipv4))
            OPENVPN_THROW_EXCEPTION(title << ".tunnel_address_index_ipv4 : IPv4 tunnel address index out of range: " << tunnel_address_index_ipv4);
        if (!validate_tunnel_index(tunnel_address_index_ipv6))
            OPENVPN_THROW_EXCEPTION(title << ".tunnel_address_index_ipv6 : IPv6 tunnel address index out of range: " << tunnel_address_index_ipv6);
        const RouteAddress *r4 = vpn_ipv4();
        if (r4 && r4->ipv6)
            OPENVPN_THROW_EXCEPTION(title << ".tunnel_address_index_ipv4 : IPv4 tunnel address index points to wrong address type: " << r4->to_string());
        const RouteAddress *r6 = vpn_ipv6();
        if (r6 && !r6->ipv6)
            OPENVPN_THROW_EXCEPTION(title << ".tunnel_address_index_ipv6 : IPv6 tunnel address index points to wrong address type: " << r6->to_string());
    }

    void validate_mtu(const std::string &title) const
    {
        if (mtu < 0 || mtu > mtu_ipv4_maximum)
            OPENVPN_THROW_EXCEPTION(title << ".mtu : MTU out of range: " << mtu);
    }

    void validate_layer(const std::string &title) const
    {
        if (!layer.defined())
            OPENVPN_THROW_EXCEPTION(title << ": layer undefined");
    }
};
} // namespace openvpn

#endif
