#ifndef HiggsAnalysis_CombinedLimit_CachingNLL_h
#define HiggsAnalysis_CombinedLimit_CachingNLL_h

#include <memory>
#include <map>
#include <RooAbsPdf.h>
#include <RooAddPdf.h>
#include <RooRealSumPdf.h>
#include <RooProdPdf.h>
#include <RooAbsData.h>
#include <RooArgSet.h>
#include <RooSetProxy.h>
#include <RooRealVar.h>
#include <RooSimultaneous.h>
#include <RooGaussian.h>
#include <RooPoisson.h>
#include <RooProduct.h>
#include "SimpleGaussianConstraint.h"
#include "SimplePoissonConstraint.h"
#include "SimpleConstraintGroup.h"

class RooMultiPdf;

// Part zero: ArgSet checker
namespace cacheutils {
    class ArgSetChecker {
        public:
            ArgSetChecker() {}
            ArgSetChecker(const RooAbsCollection &set) ;
            bool changed(bool updateIfChanged=false) ;
        private:
            std::vector<RooRealVar *> vars_;
            std::vector<double> vals_;
            std::vector<RooCategory *> cats_;
            std::vector<int> states_;
    };

// Part zero point five: Cache of pdf values for different parameters
    class ValuesCache {
        public:
            ValuesCache(const RooAbsReal &pdf, const RooArgSet &obs, int size=MaxItems_);
            ValuesCache(const RooAbsCollection &params, int size=MaxItems_);
            ~ValuesCache();
            // search for the item corresponding to the current values of the parameters.
            // if available, return (&values, true)
            // if not available, return (&room, false)
            // and it will be up to the caller code to fill the room the new item
            std::pair<std::vector<Double_t> *, bool> get(); 
            void clear();
            inline void setDirectMode(bool mode) { directMode_ = mode; }
        private:
            struct Item {
                Item(const RooAbsCollection &set)   : checker(set),   good(false) {}
                Item(const ArgSetChecker    &check) : checker(check), good(false) {}
                std::vector<Double_t> values;
                ArgSetChecker         checker;
                bool                  good;
            };
            int size_, maxSize_;
            enum { MaxItems_ = 3 };
            Item *items[MaxItems_];
            bool directMode_;
    };
// Part one: cache all values of a pdf
class CachingPdfBase {
    public:
        CachingPdfBase() {}
        virtual ~CachingPdfBase() {}
        virtual const std::vector<Double_t> & eval(const RooAbsData &data) = 0;
        virtual const RooAbsReal *pdf() const = 0;
        virtual void  setDataDirty() = 0;
        virtual void  setIncludeZeroWeights(bool includeZeroWeights) = 0;
};
class CachingPdf : public CachingPdfBase {
    public:
        CachingPdf(RooAbsReal *pdf, const RooArgSet *obs) ;
        CachingPdf(const CachingPdf &other) ;
        ~CachingPdf() override ;
        const std::vector<Double_t> & eval(const RooAbsData &data) override ;
        const RooAbsReal *pdf() const override { return pdf_; }
        void  setDataDirty() override { lastData_ = 0; }
        void  setIncludeZeroWeights(bool includeZeroWeights) override { includeZeroWeights_ = includeZeroWeights;  setDataDirty(); }
    protected:
        const RooArgSet *obs_;
        RooAbsReal *pdfOriginal_;
        RooArgSet  pdfPieces_;
        RooAbsReal *pdf_;
        const RooAbsData *lastData_ = 0;
        ValuesCache cache_;
        std::vector<uint8_t> nonZeroW_;
        unsigned int         nonZeroWEntries_;
        bool                 includeZeroWeights_ = false;
        virtual void newData_(const RooAbsData &data) ;
        virtual void realFill_(const RooAbsData &data, std::vector<Double_t> &values) ;
};

template <typename PdfT, typename VPdfT> 
class OptimizedCachingPdfT : public CachingPdf {
    public:
        OptimizedCachingPdfT(RooAbsReal *pdf, const RooArgSet *obs) :
            CachingPdf(pdf,obs), vpdf_(0) {}
        OptimizedCachingPdfT(const OptimizedCachingPdfT &other) : 
            CachingPdf(other), vpdf_(0) {}
        ~OptimizedCachingPdfT() override { delete vpdf_; }
    protected:
        void realFill_(const RooAbsData &data, std::vector<Double_t> &values) override ;
        void newData_(const RooAbsData &data) override ;
        VPdfT *vpdf_;
};

CachingPdfBase * makeCachingPdf(RooAbsReal *pdf, const RooArgSet *obs) ;

class CachingAddNLL : public RooAbsReal {
    public:
        CachingAddNLL(const char *name, const char *title, RooAbsPdf *pdf, RooAbsData *data, bool includeZeroWeights = false) ;
        CachingAddNLL(const CachingAddNLL &other, const char *name = 0) ;
        ~CachingAddNLL() override ;
        CachingAddNLL *clone(const char *name = 0) const override ;
        Double_t evaluate() const override ;
        Bool_t isDerived() const override { return kTRUE; }
        Double_t defaultErrorLevel() const override { return 0.5; }
        void setData(const RooAbsData &data) ;
        virtual RooArgSet* getObservables(const RooArgSet* depList, Bool_t valueOnly = kTRUE) const ;
#if ROOT_VERSION_CODE < ROOT_VERSION(6,26,0)
        RooArgSet* getParameters(const RooArgSet* depList, Bool_t stripDisconnected = kTRUE) const override;
#else
        bool getParameters(const RooArgSet* depList, RooArgSet& outputSet, bool stripDisconnected=true) const override;
#endif
        double  sumWeights() const { return sumWeights_; }
        const RooAbsPdf *pdf() const { return pdf_; }
        void setZeroPoint() ;
        void clearZeroPoint() ;
        void clearConstantZeroPoint() ;
        void updateZeroPoint() { clearZeroPoint(); setZeroPoint(); }
        void propagateData();
        void setAnalyticBarlowBeeston(bool flag);
        /// note: setIncludeZeroWeights(true) won't have effect unless you also re-call setData
        virtual void  setIncludeZeroWeights(bool includeZeroWeights) ;
        RooSetProxy & params() { return params_; }
        RooSetProxy & catParams() { return catParams_; }
    private:
        void setup_();
        void addPdfs_(RooAddPdf *addpdf, bool recursive, const RooArgList & basecoeffs) ;
        RooAbsPdf *pdf_;
        RooSetProxy params_, catParams_;
        const RooAbsData *data_;
        std::vector<Double_t>  weights_, binWidths_;
        double               sumWeights_;
        bool includeZeroWeights_;
        mutable std::vector<RooAbsReal*> coeffs_;
        mutable std::vector<std::unique_ptr<CachingPdfBase>>  pdfs_;
        mutable std::vector<std::unique_ptr<RooAbsReal>>  prods_;
        mutable std::vector<RooAbsReal*> integrals_;
        mutable std::vector<std::pair<const RooMultiPdf*,CachingPdfBase*> > multiPdfs_;
        mutable std::vector<Double_t> partialSum_;
        mutable std::vector<Double_t> workingArea_;
        mutable bool isRooRealSum_, fastExit_;
        mutable int canBasicIntegrals_, basicIntegrals_;
        double zeroPoint_ = 0;
        double constantZeroPoint_ = 0; // this is arbitrary and kept constant for all the lifetime of the PDF
};

class CachingSimNLL  : public RooAbsReal {
    public:
        CachingSimNLL(RooSimultaneous *pdf, RooAbsData *data, const RooArgSet *nuis=0) ;
        CachingSimNLL(const CachingSimNLL &other, const char *name = 0) ;
        ~CachingSimNLL() override ;
        CachingSimNLL *clone(const char *name = 0) const override ;
        Double_t evaluate() const override ;
        Bool_t isDerived() const override { return kTRUE; }
        Double_t defaultErrorLevel() const override { return 0.5; }
        void setData(const RooAbsData &data) ;
        virtual RooArgSet* getObservables(const RooArgSet* depList, Bool_t valueOnly = kTRUE) const ;
#if ROOT_VERSION_CODE < ROOT_VERSION(6,26,0)
        RooArgSet* getParameters(const RooArgSet* depList, Bool_t stripDisconnected = kTRUE) const override;
#else
        bool getParameters(const RooArgSet* depList, RooArgSet& outputSet, bool stripDisconnected=true) const override;
#endif
        void splitWithWeights(const RooAbsData &data, const RooAbsCategory& splitCat, Bool_t createEmptyDataSets) ;
        static void setNoDeepLogEvalError(bool noDeep) { noDeepLEE_ = noDeep; }
        void setZeroPoint() ; 
        void clearZeroPoint() ;
        void clearConstantZeroPoint() ;
        void updateZeroPoint() { clearZeroPoint(); setZeroPoint(); }
        static void forceUnoptimizedConstraints() { optimizeContraints_ = false; }
        void setChannelMasks(RooArgList const& args);
        void setAnalyticBarlowBeeston(bool flag);
        void setHideRooCategories(bool flag) { hideRooCategories_ = flag; }
        void setHideConstants(bool flag) { hideConstants_ = flag; }
        void setMaskConstraints(bool flag) ;
        void setMaskNonDiscreteChannels(bool mask) ;
        friend class CachingAddNLL;
        // trap this call, since we don't care about propagating it to the sub-components
        void constOptimizeTestStatistic(ConstOpCode opcode, Bool_t doAlsoTrackingOpt=kTRUE) override { }
    private:
        void setup_();
        RooSimultaneous   *pdfOriginal_;
        const RooAbsData  *dataOriginal_;
        const RooArgSet   *nuis_;
        RooSetProxy        params_, catParams_;
        bool hideRooCategories_ = false;
        bool hideConstants_ = false;
        RooArgSet piecesForCloning_;
        std::unique_ptr<RooSimultaneous>  factorizedPdf_;
        std::vector<RooAbsPdf *>        constrainPdfs_;
        std::vector<SimpleGaussianConstraint *>  constrainPdfsFast_;
        std::vector<bool>                        constrainPdfsFastOwned_;
        std::vector<SimplePoissonConstraint *>   constrainPdfsFastPoisson_;
        std::vector<bool>                        constrainPdfsFastPoissonOwned_;
        std::vector<SimpleConstraintGroup>       constrainPdfGroups_;
        std::vector<CachingAddNLL*>     pdfs_;
        std::unique_ptr<TList>            dataSets_;
        std::vector<RooDataSet *>       datasets_;
        static bool noDeepLEE_;
        static bool hasError_;
        static bool optimizeContraints_;
        std::vector<double> constrainZeroPoints_;
        std::vector<double> constrainZeroPointsFast_;
        std::vector<double> constrainZeroPointsFastPoisson_;
        std::vector<RooAbsReal*> channelMasks_;
        std::vector<bool>        internalMasks_;
        bool                     maskConstraints_ = false;
        RooArgSet                activeParameters_, activeCatParameters_;
        double                   maskingOffset_ = 0;     // offset to ensure that interal or constraint masking doesn't change NLL value
        double                   maskingOffsetZero_ = 0; // and associated zero point
};

}
#endif
