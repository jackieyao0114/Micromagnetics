#ifdef AMREX_USE_CUDA
#include <cufft.h>
#else
#include <fftw3.h>
#include <fftw3-mpi.h>
#endif

#include "MagnetostaticSolver.H"
#include "CartesianAlgorithm.H"

void ComputePoissonRHS(MultiFab&                        PoissonRHS,
                       Array<MultiFab, AMREX_SPACEDIM>& Mfield,
                       MultiFab& Ms,
                       const Geometry&                 geom)
{
    for ( MFIter mfi(PoissonRHS); mfi.isValid(); ++mfi )
        {
            const Box& bx = mfi.validbox();
            // extract dx from the geometry object
            GpuArray<Real,AMREX_SPACEDIM> dx = geom.CellSizeArray();

            const Array4<Real const>& Mx = Mfield[0].array(mfi);         
            const Array4<Real const>& My = Mfield[1].array(mfi);         
            const Array4<Real const>& Mz = Mfield[2].array(mfi);   

            const Array4<Real>& rhs = PoissonRHS.array(mfi);

            amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE (int i, int j, int k)
            {

               rhs(i,j,k) =  DivergenceDx_Mag(Mx, i, j, k, dx)
                           + DivergenceDy_Mag(My, i, j, k, dx)
                           + DivergenceDz_Mag(Mz, i, j, k, dx);
                
            });
        }

}

void ComputeHfromPhi(MultiFab&                        PoissonPhi,
                     Array<MultiFab, AMREX_SPACEDIM>& H_demagfield,
                     amrex::GpuArray<amrex::Real, 3>  prob_lo,
                     amrex::GpuArray<amrex::Real, 3>  prob_hi,
                     const Geometry&                  geom)
{
       // Calculate H from Phi

        for ( MFIter mfi(PoissonPhi); mfi.isValid(); ++mfi )
        {
            const Box& bx = mfi.validbox();

            // extract dx from the geometry object
            GpuArray<Real,AMREX_SPACEDIM> dx = geom.CellSizeArray();

            const Array4<Real>& Hx_demag = H_demagfield[0].array(mfi);
            const Array4<Real>& Hy_demag = H_demagfield[1].array(mfi);
            const Array4<Real>& Hz_demag = H_demagfield[2].array(mfi);

            const Array4<Real>& phi = PoissonPhi.array(mfi);

            amrex::ParallelFor( bx, [=] AMREX_GPU_DEVICE (int i, int j, int k)
            {
                     Hx_demag(i,j,k) = -(phi(i+1,j,k) - phi(i-1,j,k))/2.0/(dx[0]);
                     Hy_demag(i,j,k) = -(phi(i,j+1,k) - phi(i,j-1,k))/2.0/(dx[1]);
                     Hz_demag(i,j,k) = -(phi(i,j,k+1) - phi(i,j,k-1))/2.0/(dx[2]); // consider using GetGradSolution function from amrex
             });
        }

}







/*
// Function accepts the geometry of the problem and then defines the demagnetization tensor in space.  
void ComputeDemagTensor(MultiFab&                        Kxx,
		        MultiFab&                        Kxy,
		        MultiFab&                        Kxz,
		        MultiFab&                        Kyy,
		        MultiFab&                        Kyz,
		        MultiFab&                        Kzz,
                        GpuArray<int, 3>                 n_cell,
                        amrex::GpuArray<amrex::Real, 3>  prob_lo,
                        amrex::GpuArray<amrex::Real, 3>  prob_hi,
                        int                              max_grid_size,
                        const Geometry&                  geom)
{
    // **********************************
    // DEFINE SIMULATION SETUP AND GEOMETRY
    // **********************************
    // make BoxArray and Geometry
    // ba will contain a list of boxes that cover the domain
    // geom contains information such as the physical domain size,
    // number of points in the domain, and periodicity
    BoxArray ba;
    Geometry geom;

    // Define lower and upper indices
    // Demag tensor has twice as many entries in each respective direction
    IntVect dom_lo(AMREX_D_DECL(       0,        0,        0));
    IntVect dom_hi(AMREX_D_DECL(2*n_cell[0]-1, 2*n_cell[1]-1, 2*n_cell[2]-1));

    // Make a single box that is the entire domain
    Box domain(dom_lo, dom_hi);

    // Initialize the boxarray "ba" from the single box "domain"
    ba.define(domain);

    // Break up boxarray "ba" into chunks no larger than "max_grid_size" along a direction
    ba.maxSize(max_grid_size);

    // How Boxes are distrubuted among MPI processes
    DistributionMapping dm(ba);

    // This defines the physical box size in each direction
    RealBox real_box({ AMREX_D_DECL(prob_lo[0], prob_lo[1], prob_lo[2])},
                     { AMREX_D_DECL(prob_hi[0], prob_hi[1], prob_hi[2])});
    
    // Enfore open b.c. in all directions ??????????????

    // extract dx from the geometry object
    GpuArray<Real,AMREX_SPACEDIM> dx = geom.CellSizeArray();

    // MultiFab storage for the demag tensor
    MultiFab Kxx (ba, dm, 1, 0);
    MultiFab Kxy (ba, dm, 1, 0);
    MultiFab Kxz (ba, dm, 1, 0);
    MultiFab Kyy (ba, dm, 1, 0);
    MultiFab Kyz (ba, dm, 1, 0);
    MultiFab Kzz (ba, dm, 1, 0);

    Real prefactor = 1. / 4. / 3.14159265;

    // Loop through demag tensor and fill with values
    for (MFIter mfi(Kxx); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.validbox();

        const Array4<Real>& Kxx_ptr = Kxx.array(mfi);
        const Array4<Real>& Kxy_ptr = Kxy.array(mfi);
        const Array4<Real>& Kxz_ptr = Kxz.array(mfi);
        const Array4<Real>& Kyy_ptr = Kyy.array(mfi);
        const Array4<Real>& Kyz_ptr = Kyz.array(mfi);
        const Array4<Real>& Kzz_ptr = Kzz.array(mfi);

        // Set the demag tensor
        ParallelFor(bx, [=] AMREX_GPU_DEVICE(int L, int M, int N)
        {
            // Scalars that will represent the indices of a positive and negative coordinate system
            int I = L - n_cell[0];
            int J = M - n_cell[1];
            int K = N - n_cell[2];

            // **********************************
            // SET VALUES FOR EACH CELL
            // **********************************
            for (int i = 0; i <= 1; i++) { // helper indices
                for (int j = 0; j <= 1; j++) { 
                    for (int k = 0; k <= 1; k++) { 
                        r = std::sqrt ((I+i-0.5)*(I+i-0.5)*dx*dx + (J+j-0.5)*(J+j-0.5)*dy*dy + (K+k-0.5)*(K+k-0.5)*dz*dz);
                        
                        Kxx(L,M,N) = Kxx(L,M,N) + std::pow(-1,i+j+k) * std::atan ((K+k-0.5) * (J+j-0.5) * dz * dy / r / (I+i-0.5) / dx);
                        
                        Kxy(L,M,N) = Kxy(L,M,N) + std::pow(-1,i+j+k) * std::log ((K+k-0.5) * dz + r);
                        
                        Kxz(L,M,N) = Kxz(L,M,N) + std::pow(-1,i+j+k) * std::log ((J+j-0.5) * dy + r);
                        
                        Kyy(L,M,N) = Kyy(L,M,N) + std::pow(-1,i+j+k) * std::atan ((I+i-0.5) * (K+k-0.5) * dx * dz / r / (J+j-0.5) / dy);
                        
                        Kyz(L,M,N) = Kyz(L,M,N) + std::pow(-1,i+j+k) * std::log ((I+i-0.5) * dx + r);
                        
                        Kzz(L,M,N) = Kzz(L,M,N) + std::pow(-1,i+j+k) * std::atan ((J+j-0.5) * (I+i-0.5) * dy * dx / r / (K+k-0.5) / dz);
                    }
                }
            }

            Kxx(L,M,N) = Kxx(L,M,N) * prefactor;
            Kxy(L,M,N) = Kxy(L,M,N) * (-prefactor);
            Kxz(L,M,N) = Kxz(L,M,N) * (-prefactor);
            Kyy(L,M,N) = Kyy(L,M,N) * prefactor;
            Kyz(L,M,N) = Kyz(L,M,N) * (-prefactor);
            Kzz(L,M,N) = Kzz(L,M,N) * prefactor;

        });
    }

    
}
*/







/*
// THIS COMES LAST!!!!!!!!! COULD BE THE TRICKY PART...
// We will call the three other functions from within this function... which will be called from 'main.cpp' at each time step
// Convolving the fft of the magnetization and the fft of the demag tensor and then taking the inverse of that convolution, with the form outlined below 
// Hx = ifftn(fftn(Mx) .* Kxx_fft + fftn(My) .* Kxy_fft + fftn(Mz) .* Kxz_fft); % calc demag field with fft
// Hy = ifftn(fftn(Mx) .* Kxy_fft + fftn(My) .* Kyy_fft + fftn(Mz) .* Kyz_fft);
// Hz = ifftn(fftn(Mx) .* Kxz_fft + fftn(My) .* Kyz_fft + fftn(Mz) .* Kzz_fft);
void ComputeHFieldFFT(const Array<MultiFab, AMREX_SPACEDIM>& M_field,
	              Array<MultiFab, AMREX_SPACEDIM>&       H_demagfield,
                      const MultiFab&                        Kxx_fft,
		      const MultiFab&                        Kxy_fft,
		      const MultiFab&                        Kxz_fft,
		      const MultiFab&                        Kyy_fft,
		      const MultiFab&                        Kyz_fft,
		      const MultiFab&                        Kzz_fft,
                      GpuArray<Real, 3>                      prob_lo,
                      GpuArray<Real, 3>                      prob_hi,
                      GpuArray<int, 3>                       n_cell,
		      int                                    max_grid_size,
                      const Geometry&                        geom)
{
   
    // **********************************
    // COPY INPUT MULTIFAB INTO A MULTIFAB WITH ONE BOX
    // **********************************

    // Calculate the Mx, My, and Mz fft's at the current time step     
    ComputeForwardFFT(M_field[0], mf_dft_real_x, mf_dft_imag_x, prob_lo, prob_hi, n_cell, max_grid_size, geom);
    ComputeForwardFFT(M_field[1], mf_dft_real_y, mf_dft_imag_y, prob_lo, prob_hi, n_cell, max_grid_size, geom);
    ComputeForwardFFT(M_field[2], mf_dft_real_z, mf_dft_imag_z, prob_lo, prob_hi, n_cell, max_grid_size, geom);

    MultiFab mf_dft_real(ba, dm, 1, 0);
    MultiFab mf_dft_imag(ba, dm, 1, 0);

    for ( MFIter mfi(M_field[0]); mfi.isValid(); ++mfi )
        {
            const Box& bx = mfi.validbox();

            // extract dx from the geometry object
            GpuArray<Real,AMREX_SPACEDIM> dx = geom.CellSizeArray();

            const Array4<Real>& Hx_demag_conv_real = H_demagfield[0].array(mfi);
            const Array4<Real>& Hy_demag_real = H_demagfield[1].array(mfi);
            const Array4<Real>& Hz_demag_real = H_demagfield[2].array(mfi);

	    // Declare pointers to the real and imaginary parts of the dft in each dimension
            const Array4<Real>& Mx_fft_real = mf_dft_real_x.array(mfi);
	    const Array4<Real>& Mx_fft_imag = mf_dft_imag_x.array(mfi);
            const Array4<Real>& My_fft_real = mf_dft_real_y.array(mfi);
	    const Array4<Real>& My_fft_imag = mf_dft_imag_y.array(mfi);
            const Array4<Real>& Mz_fft_real = mf_dft_real_z.array(mfi);
	    const Array4<Real>& Mz_fft_imag = mf_dft_imag_z.array(mfi);

            amrex::ParallelFor( bx, [=] AMREX_GPU_DEVICE (int i, int j, int k)
            {
	        if  (i <= bx.length(0)/2) {     
		// Calculate dot product of demag tensor fft and magnetization fft in fourier space
                Hx_demag_conv_real(i,j,k) = ifftn(Mx_fft_real(i,j,k) * Kxx_fft_real(i,j,k) + My_fft_real(i,j,k) * Kxy_fft_real(i,j,k) + Mz_fft_real(i,j,k) * Kxz_fft_real(i,j,k));
                Hy_demag_conv_real(i,j,k) = ifftn(fftn(Mx) .* Kxy_fft + fftn(My) .* Kyy_fft + fftn(Mz) .* Kyz_fft) ;
                Hz_demag_conv_real(i,j,k) = ifftn(fftn(Mx) .* Kxz_fft + fftn(My) .* Kyz_fft + fftn(Mz) .* Kzz_fft) ;
                
                }
	    });
        }
    ComputeInverseFFT(Hx_demagfield[0], Hx_demag_conv_real, Hx_demag_conv_imag, prob_lo, prob_hi,n_cell, max_grid_size, geom);
    ComputeInverseFFT(Hy_demagfield[1], Hy_demag_conv_real, Hy_demag_conv_imag, prob_lo, prob_hi,n_cell, max_grid_size, geom);
    ComputeInverseFFT(Hz_demagfield[2], Hz_demag_conv_real, Hz_demag_conv_imag, prob_lo, prob_hi,n_cell, max_grid_size, geom); 

}
*/




// Function accepts a multifab 'mf' and computes the FFT, storing it in mf_dft_real amd mf_dft_imag multifabs
void ComputeForwardFFT(const MultiFab&    mf,
		       MultiFab&          mf_dft_real,
		       MultiFab&          mf_dft_imag,
                       GpuArray<Real, 3>  prob_lo,
                       GpuArray<Real, 3>  prob_hi,
                       GpuArray<int, 3>   n_cell,
		       int                max_grid_size,
		       const Geometry&    geom,
		       long               npts)
{ 
    // **********************************
    // COPY INPUT MULTIFAB INTO A MULTIFAB WITH ONE BOX
    // **********************************

    // create a new BoxArray and DistributionMapping for a MultiFab with 1 grid
    BoxArray ba_onegrid(geom.Domain());
    DistributionMapping dm_onegrid(ba_onegrid);

    // storage for phi and the dft
    MultiFab mf_onegrid         (ba_onegrid, dm_onegrid, 1, 0);
    MultiFab mf_dft_real_onegrid(ba_onegrid, dm_onegrid, 1, 0);
    MultiFab mf_dft_imag_onegrid(ba_onegrid, dm_onegrid, 1, 0);

    // copy phi into phi_onegrid
    mf_onegrid.ParallelCopy(mf, 0, 0, 1);

    // **********************************
    // COMPUTE FFT
    // **********************************

#ifdef AMREX_USE_CUDA
    using FFTplan = cufftHandle;
    using FFTcomplex = cuDoubleComplex;
#else
    using FFTplan = fftw_plan;
    using FFTcomplex = fftw_complex;
#endif

    // For scaling on forward FFTW
    Real sqrtnpts = std::sqrt(npts);

    // contain to store FFT - note it is shrunk by "half" in x
    Vector<std::unique_ptr<BaseFab<GpuComplex<Real> > > > spectral_field;

    Vector<FFTplan> forward_plan;

    for (MFIter mfi(mf_onegrid); mfi.isValid(); ++mfi) {

      // grab a single box including ghost cell range
      Box realspace_bx = mfi.fabbox();

      // size of box including ghost cell range
      IntVect fft_size = realspace_bx.length(); // This will be different for hybrid FFT

      // this is the size of the box, except the 0th component is 'halved plus 1'
      IntVect spectral_bx_size = fft_size;
      spectral_bx_size[0] = fft_size[0]/2 + 1;

      // spectral box
      Box spectral_bx = Box(IntVect(0), spectral_bx_size - IntVect(1));

      spectral_field.emplace_back(new BaseFab<GpuComplex<Real> >(spectral_bx,1,
                                 The_Device_Arena()));
      spectral_field.back()->setVal<RunOn::Device>(0.0); // touch the memory

      FFTplan fplan;

#ifdef AMREX_USE_CUDA

#if (AMREX_SPACEDIM == 2)
      cufftResult result = cufftPlan2d(&fplan, fft_size[1], fft_size[0], CUFFT_D2Z);
      if (result != CUFFT_SUCCESS) {
    AllPrint() << " cufftplan2d forward failed! Error: "
              << cufftErrorToString(result) << "\n";
      }
#elif (AMREX_SPACEDIM == 3)
      cufftResult result = cufftPlan3d(&fplan, fft_size[2], fft_size[1], fft_size[0], CUFFT_D2Z);
      if (result != CUFFT_SUCCESS) {
    AllPrint() << " cufftplan3d forward failed! Error: "
              << cufftErrorToString(result) << "\n";
      }
#endif

#else // host

#if (AMREX_SPACEDIM == 2)
      fplan = fftw_plan_dft_r2c_2d(fft_size[1], fft_size[0],
                   mf_onegrid[mfi].dataPtr(),
                   reinterpret_cast<FFTcomplex*>
                   (spectral_field.back()->dataPtr()),
                   FFTW_ESTIMATE);
#elif (AMREX_SPACEDIM == 3)
      fplan = fftw_plan_dft_r2c_3d(fft_size[2], fft_size[1], fft_size[0],
                   mf_onegrid[mfi].dataPtr(),
                   reinterpret_cast<FFTcomplex*>
                   (spectral_field.back()->dataPtr()),
                   FFTW_ESTIMATE);
#endif

#endif

      forward_plan.push_back(fplan);
    }

    ParallelDescriptor::Barrier();

    // ForwardTransform
    for (MFIter mfi(mf_onegrid); mfi.isValid(); ++mfi) {
      int i = mfi.LocalIndex();
#ifdef AMREX_USE_CUDA
      cufftSetStream(forward_plan[i], Gpu::gpuStream());
      cufftResult result = cufftExecD2Z(forward_plan[i],
                    mf_onegrid[mfi].dataPtr(),
                    reinterpret_cast<FFTcomplex*>
                    (spectral_field[i]->dataPtr()));
      if (result != CUFFT_SUCCESS) {
    AllPrint() << " forward transform using cufftExec failed! Error: "
              << cufftErrorToString(result) << "\n";
      }
#else
      fftw_execute(forward_plan[i]);
#endif
    }

    // copy data to a full-sized MultiFab
    // this involves copying the complex conjugate from the half-sized field
    // into the appropriate place in the full MultiFab
    for (MFIter mfi(mf_dft_real_onegrid); mfi.isValid(); ++mfi) {

      Array4< GpuComplex<Real> > spectral = (*spectral_field[0]).array();

      Array4<Real> const& realpart = mf_dft_real_onegrid.array(mfi);
      Array4<Real> const& imagpart = mf_dft_imag_onegrid.array(mfi);

      Box bx = mfi.fabbox();

      ParallelFor(bx, [=] AMREX_GPU_DEVICE (int i, int j, int k) noexcept
      {
      /*
        Copying rules:

        For domains from (0,0,0) to (Nx-1,Ny-1,Nz-1)

        For any cells with i index >= Nx/2, these values are complex conjugates of the corresponding
        entry where (Nx-i,Ny-j,Nz-k) UNLESS that index is zero, in which case you use 0.

        e.g. for an 8^3 domain, any cell with i index

        Cell (6,2,3) is complex conjugate of (2,6,5)

        Cell (4,1,0) is complex conjugate of (4,7,0)  (note that the FFT is computed for 0 <= i <= Nx/2)
      */
          if (i <= bx.length(0)/2) {
          // copy value
              realpart(i,j,k) = spectral(i,j,k).real();
              imagpart(i,j,k) = spectral(i,j,k).imag();

          }    
          realpart(i,j,k) /= sqrtnpts;
          imagpart(i,j,k) /= sqrtnpts;
      });
    }
  
    // Copy the full multifabs back into the output multifabs
    mf_dft_real.ParallelCopy(mf_dft_real_onegrid, 0, 0, 1);
    mf_dft_imag.ParallelCopy(mf_dft_imag_onegrid, 0, 0, 1);

    // destroy fft plan
    for (int i = 0; i < forward_plan.size(); ++i) {
#ifdef AMREX_USE_CUDA
        cufftDestroy(forward_plan[i]);
#else
        fftw_destroy_plan(forward_plan[i]);
#endif
     }
}











// This function takes the real and imaginary parts of data from the frequency domain and performs an inverse FFT, storing the result in 'mf_2'
// The FFTW c2r function is called which accepts complex data in the frequency domain and returns real data in the normal cartesian plane
void ComputeInverseFFT(MultiFab&                        mf_2,
		       const MultiFab&                  mf_dft_real,
                       const MultiFab&                  mf_dft_imag,				   
                       amrex::GpuArray<amrex::Real, 3>  prob_lo,
                       amrex::GpuArray<amrex::Real, 3>  prob_hi,
		       GpuArray<int, 3>                 n_cell,
                       int                              max_grid_size,
                       const Geometry&                  geom)
{

    // create a new BoxArray and DistributionMapping for a MultiFab with 1 grid
    BoxArray ba_onegrid(geom.Domain());
    DistributionMapping dm_onegrid(ba_onegrid);
    
    // Declare multifabs to store entire dataset in one grid.
    MultiFab mf_onegrid_2 (ba_onegrid, dm_onegrid, 1, 0);
    MultiFab mf_dft_real_onegrid(ba_onegrid, dm_onegrid, 1, 0);
    MultiFab mf_dft_imag_onegrid(ba_onegrid, dm_onegrid, 1, 0);

    // Copy distributed multifabs into one grid multifabs
    mf_dft_real_onegrid.ParallelCopy(mf_dft_real, 0, 0, 1);
    mf_dft_imag_onegrid.ParallelCopy(mf_dft_imag, 0, 0, 1);

#ifdef AMREX_USE_CUDA
    using FFTplan = cufftHandle;
    using FFTcomplex = cuDoubleComplex;
#else
    using FFTplan = fftw_plan;
    using FFTcomplex = fftw_complex;
#endif

// Copy mf_dft_real/imag into spectral_field!!!!!!!!!!!!
// Here I try to use the same structure employed in Poisson where we initialize a spectral_field in the loop
// Then we compute the backward plan in the loop much the way that we computed the forward in Poisson
//
//
//

    // contain to store FFT - note it is shrunk by "half" in x
    Vector<std::unique_ptr<BaseFab<GpuComplex<Real> > > > spectral_field;

    // Copy the contents of the real and imaginary FFT Multifabs into 'spectral_field'
    for (MFIter mfi(mf_dft_real_onegrid); mfi.isValid(); ++mfi) {

      // grab a single box including ghost cell range
      Box realspace_bx = mfi.fabbox();

      // size of box including ghost cell range
      IntVect fft_size = realspace_bx.length(); // This will be different for hybrid FFT

      // this is the size of the box, except the 0th component is 'halved plus 1'
      IntVect spectral_bx_size = fft_size;
      spectral_bx_size[0] = fft_size[0]/2 + 1;

      // spectral box
      Box spectral_bx = Box(IntVect(0), spectral_bx_size - IntVect(1));

      spectral_field.emplace_back(new BaseFab<GpuComplex<Real> >(spectral_bx,1,
                                 The_Device_Arena()));
      spectral_field.back()->setVal<RunOn::Device>(0.0); // touch the memory
      
        // Array4< GpuComplex<Real> > spectral = (*spectral_field[0]).array();
        Array4< GpuComplex<Real> > spectral = (*spectral_field[0]).array();

        Array4<Real> const& realpart = mf_dft_real_onegrid.array(mfi);
        Array4<Real> const& imagpart = mf_dft_imag_onegrid.array(mfi);

        Box bx = mfi.fabbox();

        ParallelFor(bx, [=] AMREX_GPU_DEVICE (int i, int j, int k) noexcept
        {        
            if (i <= bx.length(0)/2) {
                GpuComplex<Real> copy(realpart(i,j,k),imagpart(i,j,k));
                spectral(i,j,k) = copy;
            }   
        });
    }

    // Compute the inverse FFT on spectral_field and store it in 'mf_onegrid_2'
    Vector<FFTplan> backward_plan;

    // Now that we have a spectral field full of the data from the DFT..
    // We perform the inverse DFT on spectral field and store it in mf_onegrid_2
    for (MFIter mfi(mf_onegrid_2); mfi.isValid(); ++mfi) {

       // grab a single box including ghost cell range
       Box realspace_bx = mfi.fabbox();

       // size of box including ghost cell range
       IntVect fft_size = realspace_bx.length(); // This will be different for hybrid FFT

       FFTplan bplan;

#if (AMREX_SPACEDIM == 2)
      bplan = fftw_plan_dft_c2r_2d(fft_size[1], fft_size[0],
                   reinterpret_cast<FFTcomplex*>
                   (spectral_field.back()->dataPtr()),
                   mf_onegrid_2[mfi].dataPtr(),
                   FFTW_ESTIMATE);
#elif (AMREX_SPACEDIM == 3)
      bplan = fftw_plan_dft_c2r_3d(fft_size[2], fft_size[1], fft_size[0],
                   reinterpret_cast<FFTcomplex*>
                   (spectral_field.back()->dataPtr()),
                   mf_onegrid_2[mfi].dataPtr(),
                   FFTW_ESTIMATE);
#endif

      backward_plan.push_back(bplan);// This adds an instance of bplan to the end of backward_plan
      }

    for (MFIter mfi(mf_onegrid_2); mfi.isValid(); ++mfi) {
      int i = mfi.LocalIndex();
      fftw_execute(backward_plan[i]);

      // Standard scaling after fft and inverse fft using FFTW
#if (AMREX_SPACEDIM == 2)
      mf_onegrid_2[mfi] /= n_cell[0]*n_cell[1];
#elif (AMREX_SPACEDIM == 3)
      mf_onegrid_2[mfi] /= n_cell[0]*n_cell[1]*n_cell[2];
#endif

    }

    // copy contents of mf_onegrid_2 into mf
    mf_2.ParallelCopy(mf_onegrid_2, 0, 0, 1);

    // destroy ifft plan
    for (int i = 0; i < backward_plan.size(); ++i) {
#ifdef AMREX_USE_CUDA
        cufftDestroy(backward_plan[i]);
#else
        fftw_destroy_plan(backward_plan[i]);
#endif

    }

}

